
#include "kcp_session.h"

#include <assert.h>

#include <memory>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "common/macros.h"

#include "ikcp.h"

#include "kcp_callbacks.h"
#include "kcp_packets.h"

KCPSession::KCPSession(muduo::net::EventLoop* loop)
    : loop_(CHECK_NOTNULL(loop)) {}

KCPSession::~KCPSession() {
  assert(IsClosed());
  LOG_INFO << "~KCPSession (this=" << this << ", loop_=" << loop_
           << ", session_id_=" << session_id_
           << ", base_time_=" << base_time_.toFormattedString() << ")";
}

bool KCPSession::Initialize(uint32_t session_id,
                            const muduo::net::InetAddress& peer_address,
                            const Params& params) {
  assert(kcp_.get() == nullptr);

  ScopedKCPCB kcp(ikcp_create(session_id, this));
  if (kcp.get() == nullptr) {
    return false;
  }

  ikcp_setoutput(kcp.get(), KCPSession::OnKCPOutput);
  ikcp_stream(kcp.get(), params.stream_mode);

  int rv = ikcp_wndsize(kcp.get(), params.snd_wnd, params.rcv_wnd);
  if (rv < 0) {
    return false;
  }

  rv = ikcp_setmtu(kcp.get(), params.mtu);
  if (rv < 0) {
    return false;
  }

  rv = ikcp_set_head_room(kcp.get(), params.head_room);
  if (rv < 0) {
    return false;
  }

  rv = ikcp_set_snd_hghwat(kcp.get(), params.snd_wnd);
  if (rv < 0) {
    return false;
  }

  rv = ikcp_nodelay(kcp.get(), params.nodelay, params.interval, params.resend,
                    params.nocongestion);
  if (rv < 0) {
    return false;
  }

  kcp_ = std::move(kcp);
  peer_address_ = peer_address;
  session_id_ = session_id;

  base_time_ = muduo::Timestamp::now();

  KCPSessionPtr shared_this = shared_from_this();
  loop_->runInLoop([shared_this] { shared_this->OnConnectionEvent(true); });
  loop_->queueInLoop([shared_this] { shared_this->UpdateConnectionState(); });

  return true;
}

void KCPSession::Close(bool last_flush) {
  loop_->assertInLoopThread();

  if (closed_) {
    return;
  }

  closed_ = true;
  loop_->cancel(state_timer_);

  if (last_flush) {
    ikcp_flush(kcp_.get(), CurrentMs());
    FlushTxQueue();
  }

  if (pending_error_.type > 0) {
    LOG_WARN << "session: " << session_id_
             << " closed with pending error type: " << pending_error_.type
             << ", code: " << pending_error_.code;
  }

  OnConnectionEvent(false);
}

void KCPSession::CloseAfterMs(uint32_t delay_ms, bool last_flush) {
  loop_->runAfter(static_cast<double>(delay_ms) / 1000,
                  [last_flush, shared_this = shared_from_this()] {
                    shared_this->Close(last_flush);
                  });
  LOG_INFO << "session: " << session_id_ << " will be closed after " << delay_ms
           << "ms";
}

bool KCPSession::IsClosed() const { return closed_; }

void KCPSession::UpdateConnectionState() {
  loop_->assertInLoopThread();

  if (IsClosed()) {
    return;
  }

  if (!ikcp_is_alive(kcp_.get())) {
    Close();
    return;
  }

  uint32_t wait_ms = ikcp_flush(kcp_.get(), CurrentMs());
  FlushTxQueue();

  state_timer_ = loop_->runAfter(static_cast<double>(wait_ms) / 1000,
                                 [shared_this = shared_from_this()] {
                                   shared_this->UpdateConnectionState();
                                 });
}

// wrap around
// https://tools.ietf.org/html/rfc1323#page-11
// send/recv buffer window [x, x + 2^30)
// timestamp window [x, x + 2^31)
// inline bool TimeGreaterThan(uint32_t t1, uint32_t t2) {
//   return ((t1 > t2) && (t1 - t2 < 0x80000000)) ||
//          ((t1 < t2) && (t2 - t1 > 0x80000000));
// }
uint32_t KCPSession::CurrentMs() const {
  muduo::Timestamp now = muduo::Timestamp::now();
  int64_t diff_ms =
      (now.microSecondsSinceEpoch() - base_time_.microSecondsSinceEpoch()) /
      1000;
  return static_cast<uint32_t>(diff_ms & 0xffffffff);
}

void KCPSession::OnConnectionEvent(bool connected) {
  LOG_TRACE << "kcp session " << session_id_ << " connection "
            << (connected ? "up" : "down");
  if (connection_callback_) {
    connection_callback_(shared_from_this(), connected);
  }
}

void KCPSession::FlushTxQueue() {
  LOG_TRACE << "kcp session " << session_id_ << " flush tx queue";
  if (flush_tx_queue_callback_) {
    flush_tx_queue_callback_();
  }
}

void KCPSession::OnReadEvent(size_t bytes_can_read) {
  input_buffer_.ensureWritableBytes(bytes_can_read);
  int len = ikcp_recv(kcp_.get(), input_buffer_.beginWrite(),
                      static_cast<int>(bytes_can_read));
  if (len > 0 && message_callback_) {
    input_buffer_.hasWritten(len);
    message_callback_(shared_from_this(), &input_buffer_);
  }
  assert(len > 0);
}

void KCPSession::ProcessPacket(const KCPReceivedPacket& packet,
                               const muduo::net::InetAddress& peer_address) {
  if (loop_->isInLoopThread()) {
    ProcessPacketInLoopThread(packet, peer_address);
  } else {
    KCPSessionPtr shared_this = shared_from_this();
    std::shared_ptr<KCPReceivedPacket> packet_clone =
        packet.CloneFromRemainingData();
    loop_->queueInLoop([shared_this = std::move(shared_this),
                        packet_clone = std::move(packet_clone),
                        peer_address = peer_address]() {
      shared_this->ProcessPacketInLoopThread(*packet_clone, peer_address);
    });
  }
}

void KCPSession::ProcessPacketInLoopThread(
    const KCPReceivedPacket& packet,
    const muduo::net::InetAddress& peer_address) {
  loop_->assertInLoopThread();

  if (IsClosed()) {
    LOG_ERROR << "session has already been closed, session_id: " << session_id()
              << ", packet remaining bytes: " << packet.RemainingBytes();
    return;
  }

  // we can read other types of headers from the packet when needed.
  // packet.ReadBytes(...);

  int need_drain_before_process = ikcp_need_drain(kcp_.get());
  int result = ikcp_input(kcp_.get(), packet.RemainingData(),
                          static_cast<long>(packet.RemainingBytes()));
  if (result < 0) {
    LOG_ERROR << "kcp_input error: " << result
              << ", session_id: " << session_id()
              << ", peer_address: " << peer_address.toIpPort();
    return;
  }

  // connection migration ?
  peer_address_ = peer_address;

  while (!IsClosed()) {
    int available_data_size = ikcp_peeksize(kcp_.get());
    if (available_data_size < 0) {
      break;
    }
    OnReadEvent(available_data_size);
  }

  // bool need_flush_tx_queue = false;
  // if (IsClosed() || ikcp_can_flush_after_input(kcp_.get()) > 0) {
  //   ikcp_flush(kcp_.get(), CurrentMs());
  //   need_flush_tx_queue = true;
  // } else if (ikcp_can_send_ack(kcp_.get()) > 0) {
  //   ikcp_flush_ack(kcp_.get());
  //   need_flush_tx_queue = true;
  // }

  // if (need_flush_tx_queue) {
  //   FlushTxQueue();
  // }

  if (!IsClosed()) {
    int need_drain_after_process = ikcp_need_drain(kcp_.get());
    if (need_drain_before_process > 0 && need_drain_after_process == 0 &&
        write_complete_callback_) {
      loop_->queueInLoop([shared_this = shared_from_this()]() {
        shared_this->write_complete_callback_(shared_this);
      });
    }
  }
}

void KCPSession::Write(muduo::net::Buffer* buf) {
  if (loop_->isInLoopThread()) {
    WriteInLoopThread(buf->peek(), buf->readableBytes());
    buf->retrieveAll();
  } else {
    auto data_clone =
        std::make_shared<KCPClonedPacket>(buf->peek(), buf->readableBytes());
    buf->retrieveAll();
    KCPSessionPtr shared_this = shared_from_this();
    loop_->queueInLoop([data_clone = std::move(data_clone),
                        shared_this = std::move(shared_this)]() mutable {
      shared_this->WriteInLoopThread(data_clone->data(), data_clone->length());
    });
  }
}

void KCPSession::Write(const void* data, size_t len) {
  if (loop_->isInLoopThread()) {
    WriteInLoopThread(data, len);
  } else {
    auto data_clone = std::make_shared<KCPClonedPacket>(data, len);
    KCPSessionPtr shared_this = shared_from_this();
    loop_->queueInLoop([data_clone = std::move(data_clone),
                        shared_this = std::move(shared_this)]() mutable {
      shared_this->WriteInLoopThread(data_clone->data(), data_clone->length());
    });
  }
}

void KCPSession::WriteInLoopThread(const void* data, size_t len) {
  loop_->assertInLoopThread();

  if (IsClosed()) {
    LOG_ERROR << "session has already been closed, session_id: " << session_id()
              << ", data len: " << len;
    return;
  }

  size_t bytes_write = 0;
  size_t bytes_remaining = len;

  int reach_snd_hghwat_before_process = ikcp_reach_snd_hghwat(kcp_.get());

  if (ikcp_need_drain(kcp_.get()) == 0) {
    size_t bytes_can_write = ikcp_available_wnd_in_bytes(kcp_.get());
    bytes_can_write = std::min(bytes_can_write, len);

    if (bytes_can_write > 0) {
      size_t bytes_can_write_to_wire =
          ikcp_available_sndwnd_in_bytes(kcp_.get());

      int result = ikcp_send(kcp_.get(), static_cast<const char*>(data),
                             static_cast<int>(bytes_can_write));
      if (result == 0) {
        bytes_write = bytes_can_write;
        if (bytes_can_write_to_wire > 0) {
          ikcp_flush(kcp_.get(), CurrentMs());
          FlushTxQueue();
        }

        bytes_remaining -= bytes_write;
        if (bytes_remaining == 0 && write_complete_callback_) {
          loop_->queueInLoop([shared_this = shared_from_this()]() {
            shared_this->write_complete_callback_(shared_this);
          });
        }
      }
      assert(result == 0);
    }
  }

  if (bytes_remaining > 0) {
    LOG_WARN << "bytes_remaining: " << bytes_remaining;
    int result =
        ikcp_append(kcp_.get(), static_cast<const char*>(data) + bytes_write,
                    static_cast<int>(bytes_remaining));
    if (result == 0) {
      int reach_snd_hghwat_after_process = ikcp_reach_snd_hghwat(kcp_.get());
      if (reach_snd_hghwat_before_process == 0 &&
          reach_snd_hghwat_after_process > 0 && high_water_mark_callback_) {
        size_t waitsnd = ikcp_waitsnd(kcp_.get());
        loop_->queueInLoop([shared_this = shared_from_this(), waitsnd] {
          shared_this->high_water_mark_callback_(shared_this, waitsnd);
        });
        LOG_WARN << "reach high water mark, session_id: " << session_id()
                 << ", waitsnd: " << waitsnd;
      }
    }
    assert(result == 0);
  }
}

int KCPSession::OnKCPOutput(char* buf, int len, IKCPCB* kcp, void* user) {
  UNUSED(kcp);

  KCPSession* session = static_cast<KCPSession*>(user);
  session->output_callback_(buf, static_cast<size_t>(len),
                            session->session_id(), session->peer_address());
  return 0;
}
