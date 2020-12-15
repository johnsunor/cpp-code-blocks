#include <unistd.h>

#include <stdio.h>

#include <algorithm>
#include <sstream>
#include <vector>

#include <muduo/base/Logging.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "kcp_callbacks.h"
#include "kcp_client.h"
#include "kcp_session.h"
#include "urandom.h"

int main(int argc, char* argv[]) {
  using namespace std;
  using namespace muduo;
  using namespace muduo::net;

  if (argc != 4) {
    fprintf(stderr, "Usage: client <host_ip> <port> <count>\n");
  } else {
    LOG_INFO << "pid = " << getpid() << ", tid = " << CurrentThread::tid();
    Logger::setLogLevel(Logger::WARN);

    const char* ip = argv[1];
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    uint32_t count = static_cast<uint32_t>(atoi(argv[3]));

    EventLoop loop;
    InetAddress server_address(ip, port);

    auto rand_between = [](uint32_t a, uint32_t b) {
      assert(a <= b);

      uint64_t range = static_cast<uint64_t>(b) - a + 1;

      uint32_t num;
      URandom::GetInstance().RandBytes(&num, sizeof(num));

      uint32_t result = a + static_cast<uint32_t>(num % range);
      return result;
    };

    auto split_into = [&](const string& msg, size_t n) {
      assert(msg.size() >= n && n >= 1);

      vector<string> vs;
      vector<size_t> vi;

      vs.reserve(n);
      vi.reserve(msg.size() + 1);

      for (size_t i = 0; i < msg.size(); ++i) {
        vi.push_back(i);
      }
      for (size_t i = 1; i < n; ++i) {
        auto t = rand_between(static_cast<uint32_t>(i),
                              static_cast<uint32_t>(msg.size() - 1));
        swap(vi[i], vi[t]);
      }

      sort(vi.begin(), vi.begin() + n);

      vi[n] = msg.size();

      size_t size = 0;
      ostringstream os;
      for (size_t i = 0; i < n; ++i) {
        size_t new_size = vi[i + 1] - vi[i];
        size += new_size;
        vs.emplace_back(msg.substr(vi[i], new_size));
        os << "[" << vi[i] << ", " << vi[i] + new_size << ") ";
      }
      os << "[0, " << msg.size() << ")";
      // LOG_WARN << os.str();

      assert(size == msg.size());

      return vs;
    };

    uint32_t msg_id = 0;
    string last_msg;
    auto send_next_mext_msg = [&](const KCPSessionPtr& session) {
      if (msg_id >= count) {
        LOG_WARN << "all passed";
        session->CloseAfterMs(1000, true);
        return;
      }

      {
        string msg;
        last_msg.swap(msg);
      }

      uint32_t msg_size = rand_between(10, 10 * kMaxPacketSize);

      last_msg.reserve(msg_size);
      for (uint32_t i = 0; i < msg_size; ++i) {
        last_msg.push_back(static_cast<char>(rand_between(32, 126)));
      }

      vector<string> msgs = split_into(last_msg, 10);
      for (const auto& m : msgs) {
        session->Write(m.data(), m.size());
        usleep(10000);
      }

      ++msg_id;
    };

    KCPClient client(&loop);
    client.set_connection_callback(
        [&](const KCPSessionPtr& session, bool connected) {
          if (connected) {
            send_next_mext_msg(session);
          } else {
            session->loop()->quit();
          }
        });

    client.set_message_callback([&](const KCPSessionPtr& session, Buffer* buf) {
      if (buf->readableBytes() < last_msg.size()) {
        return;
      }

      if (buf->readableBytes() > last_msg.size()) {
        LOG_FATAL << "unexpected msg size: " << buf->readableBytes()
                  << ", expected: " << last_msg.size();
      }

      string received_msg = buf->retrieveAllAsString();
      if (received_msg != last_msg) {
        LOG_FATAL << "unexpected msg: " << buf->retrieveAllAsString()
                  << ", expected: " << last_msg;
      }

      LOG_WARN << "msg_id: " << msg_id << " msg_size: " << last_msg.size()
               << " ok";
      send_next_mext_msg(session);
    });

    client.ConnectOrDie(server_address);

    loop.loop();
  }
}
