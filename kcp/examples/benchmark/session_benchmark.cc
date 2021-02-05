
#include <memory>
#include <vector>

#include <muduo/base/CurrentThread.h>
#include <muduo/base/Logging.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/InetAddress.h>

#include "common/macros.h"

#include "kcp_callbacks.h"
#include "kcp_constants.h"
#include "kcp_packets.h"
#include "kcp_session.h"
#include "log_util.h"

class SessionBenchmark final {
 public:
  explicit SessionBenchmark(muduo::net::EventLoop* loop)
      : base_loop_(CHECK_NOTNULL(loop)) {}

  void Initialize(int num_sessions, int num_threads, int message_size,
                  int mtu) {
    ASSERT_EXIT(num_sessions > 0 && (num_sessions & 0x01) == 0);
    ASSERT_EXIT(num_threads >= 0);
    ASSERT_EXIT(message_size > 0);
    ASSERT_EXIT(mtu > 50);

    pipes_.fds.resize(num_sessions);
    for (int i = 0, l = num_sessions / 2; i < l; ++i) {
      auto ret = HANDLE_EINTR(
          ::socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0,
                       &pipes_.fds[2 * i]));
      if (ret < 0) {
        LOG_SYSFATAL << "::socketpair";
      }
    }

    thread_pool_ = std::make_unique<muduo::net::EventLoopThreadPool>(
        base_loop_, "benchmark");
    thread_pool_->setThreadNum(num_threads);
    thread_pool_->start();

    message_.reserve(message_size);
    for (int i = 0; i < message_size; ++i) {
      message_.push_back(static_cast<char>(i % 128));
    }

    session_params_ = kNormalModeKCPParams;
    session_params_.mtu = mtu;

    // unused arguments in lambda expressed as auto
    for (int i = 0; i < num_sessions; ++i) {
      auto loop = thread_pool_->getLoopForHash(i);
      auto session = std::make_shared<KCPSession>(loop);

      session->set_connection_callback(
          [=](const KCPSessionPtr& sess, bool connected) {
            OnConnection(i, sess, connected);
          });
      session->set_message_callback(
          [=](const KCPSessionPtr& sess, muduo::net::Buffer* buf) {
            OnMessage(i, sess, buf);
          });

      session->set_output_callback(
          [=](const void* data, size_t length, auto, const auto&) {
            SendData(i, data, length);
          });
      sessions_.emplace_back(std::move(session));

      auto channel = std::make_unique<muduo::net::Channel>(loop, pipes_.fds[i]);
      channel->setReadCallback([=](auto) { OnRead(i); });
      channel->setErrorCallback([] { LOG_FATAL << "pipe error"; });
      channels_.emplace_back(std::move(channel));
    }
  }

  void Run(double timeout_sec) {
    static const muduo::net::InetAddress dummy;
    const int num_sessions = static_cast<int>(sessions_.size());
    for (int i = 0; i < num_sessions; ++i) {
      auto loop = thread_pool_->getLoopForHash(i);
      ASSERT_EXIT(sessions_[i]->Initialize(i / 2, dummy, session_params_));
      loop->runInLoop([=] { channels_[i]->enableReading(); });
    }

    base_loop_->runAfter(timeout_sec, [=] {
      uint64_t total_bytes_read = total_bytes_read_;
      uint64_t total_messages_read = total_messages_read_;

      LOG_INFO << total_bytes_read << " total bytes read";
      LOG_INFO << total_messages_read << " total messages read";

      if (total_messages_read > 0) {
        LOG_INFO << static_cast<double>(total_bytes_read) /
                        static_cast<double>(total_messages_read)
                 << " average message size";
      }

      if (timeout_sec > 0) {
        LOG_INFO << static_cast<double>(total_bytes_read) /
                        (timeout_sec * 1024 * 1024)
                 << " MiB/s throughput";
      }

      Close();

      base_loop_->quit();
    });
  }

  ~SessionBenchmark() {
    Close();

    // ~thread_pool_
  }

 private:
  void OnMessage(int session_no, const KCPSessionPtr& session,
                 muduo::net::Buffer* buf) const {
    const uint64_t message_bytes = buf->readableBytes();
    if ((session_no & 0x01) == 0) {
      ++total_messages_read_;
      total_bytes_read_ += message_bytes;
      total_bytes_write_ += message_bytes;
    }
    session->Write(buf);
    LOG_DEBUG << "session_no: " << session_no
              << ", message_bytes: " << message_bytes;
  }

  void SendData(int session_no, const void* data, size_t length) const {
    ASSERT_EXIT(session_no >= 0 &&
                session_no < static_cast<int>(sessions_.size()));
    if (UNLIKELY(pipes_.fds[session_no] == -1)) {
      return;
    }

    auto bytes_sent =
        HANDLE_EINTR(::write(pipes_.fds[session_no], data, length));
    if (bytes_sent < 0) {
      LOG_SYSFATAL << "::send";
    }
    // ASSERT_EXIT(static_cast<size_t>(bytes_sent) == length);
    LOG_DEBUG << "session_no: " << session_no << ", bytes_sent: " << bytes_sent;
  }

  void OnConnection(int session_no, const KCPSessionPtr& session,
                    bool connected) const {
    if (connected) {
      LOG_INFO << "session " << session->session_id() << " up";
      if ((session_no & 0x01) == 0) {
        session->Write(message_.data(), message_.size());
      }
    } else {
      LOG_INFO << "session " << session->session_id() << " down";
    }
  }

  void OnRead(int session_no) const {
    ASSERT_EXIT(session_no >= 0 &&
                session_no < static_cast<int>(sessions_.size()));
    if (UNLIKELY(pipes_.fds[session_no] == -1)) {
      return;
    }

    static muduo::net::InetAddress dummy;

    char buf[message_.size()];
    while (true) {
      auto bytes_read =
          HANDLE_EINTR(::read(pipes_.fds[session_no], buf, sizeof(buf)));
      if (bytes_read > 0) {
        KCPReceivedPacket packet(buf, bytes_read);
        sessions_[session_no]->ProcessPacket(packet, dummy);
      } else if (bytes_read < 0) {
        if (IS_EAGAIN(errno)) {
          break;
        }
        // else
        LOG_SYSFATAL << "::read";
      }
      LOG_DEBUG << "session_no: " << session_no
                << ", bytes_read: " << bytes_read;
    }
  }

  void Close() {
    base_loop_->assertInLoopThread();
    if (closed_) {
      return;
    }

    closed_ = true;
    std::atomic<int> num_closed_sessions{0};
    int num_sessons = static_cast<int>(sessions_.size());
    for (int i = 0; i < num_sessons; ++i) {
      auto loop = thread_pool_->getLoopForHash(i);
      loop->runInLoop([=, &num_closed_sessions] {
        channels_[i]->disableAll();
        channels_[i]->remove();
        sessions_[i]->Close();
        ++num_closed_sessions;
      });
    }

    // spin
    while (num_closed_sessions != num_sessons)
      muduo::CurrentThread::sleepUsec(100);
  }

  struct PipeVec {
    PipeVec() = default;
    ~PipeVec() {
      for (auto& fd : fds) {
        ignore_result(::close(fd));
        fd = -1;
      }
    }
    std::vector<int> fds;
  };

  muduo::net::EventLoop* const base_loop_{nullptr};

  mutable std::atomic<uint64_t> total_bytes_read_{0};
  mutable std::atomic<uint64_t> total_bytes_write_{0};
  mutable std::atomic<uint64_t> total_messages_read_{0};

  PipeVec pipes_;
  std::string message_;
  KCPSession::Params session_params_;
  std::vector<KCPSessionPtr> sessions_;
  std::vector<std::unique_ptr<muduo::net::Channel>> channels_;
  std::unique_ptr<muduo::net::EventLoopThreadPool> thread_pool_;

  bool closed_{false};

  DISALLOW_COPY_AND_ASSIGN(SessionBenchmark);
};

int main(int argc, char* argv[]) {
  if (argc != 6) {
    fprintf(stderr,
            "Usage: %s <num_sessons> <num_threads> <message_size> <mtu> "
            "<timeout_sec>\n",
            argv[0]);
    return 0;
  }

  int num_sessions = atoi(argv[1]);
  int num_threads = atoi(argv[2]);
  int message_size = atoi(argv[3]);
  int mtu = atoi(argv[4]);
  double timeout_sec = atof(argv[5]);
  ASSERT_EXIT(num_sessions > 0 && num_sessions <= 10 &&
              (num_sessions & 0x01) == 0);
  ASSERT_EXIT(num_threads >= 0 && num_threads <= 10);
  ASSERT_EXIT(message_size > 0 && message_size <= 32 * 1024);
  ASSERT_EXIT(mtu > 50 && mtu <= message_size);
  ASSERT_EXIT(timeout_sec > 0 && timeout_sec <= 3600);

  // muduo::Logger::setLogLevel(muduo::Logger::DEBUG);

  LOG_INFO << "pid = " << getpid() << ", tid = " << muduo::CurrentThread::tid();

  muduo::net::EventLoop loop;

  SessionBenchmark bc(&loop);
  bc.Initialize(num_sessions, num_threads, message_size, mtu);
  bc.Run(timeout_sec);

  loop.loop();

  return 0;
}
