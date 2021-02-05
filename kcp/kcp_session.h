#ifndef KCP_SESSION_H
#define KCP_SESSION_H

#include <stdint.h>

#include <atomic>
#include <functional>
#include <memory>

#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TimerId.h>

#include "common/macros.h"

#include "ikcp.h"

#include "kcp_callbacks.h"
#include "kcp_constants.h"
#include "kcp_packets.h"

namespace muduo {
namespace net {

class EventLoop;
}
}  // namespace muduo

struct KCPPendingSession {
  uint32_t session_id{0};
  uint32_t retry_times{0};
  muduo::Timestamp syn_received_time;
  muduo::Timestamp syn_sent_time;
  muduo::net::InetAddress peer_address;
};

// icmp error
struct PendingError {
  uint8_t type{0};
  uint8_t code{0};
};

// diagram
//
// |<--------snd_que------->|<-snd_buf->|                  |<-rcv_buf->|<-----rcv_que------>|
// +------------------------+-----------+--------+     +---+--------------------------------+
// |n|n-1|...|16|15|14|13|12|11|10|09|08|........| ==> |...|xx|xx|xx|xx|07|06|05|04|03|02|01|
// +------------------------+-----------+--------+     +---+--------------------------------+
//            ^           ^            ^         ^     ^              ^       nrcv_que      ^
//            |           |            |         |     |              |<--------acked------>|
//            |           |        [snd_una]     |     |              |                     |
//            |           |            |         |     |          [rcv_nxt]                 |
//            |       [snd_nxt]        |         |     |              |                     |
//            |           |            |         |     |<----rwnd---->|                     |
//            |<--dsnd--->|<-inflight->|         |     |                                    |
//            |                        |         |     |                                    |
//            |<---------wnd---------->|<-acked->|     |<---------------rcv_wnd-------------|
//
// sender:
// inflight = snd_una - snd_nxt => [08, 12)
// wnd = min(cwnd, min(swnd, rwnd)) => [08, 16)
// dsnd = snd_una + wnd - snd_nxt => [12, 16)
//
// recever:
// rwnd = rcv_wnd - nrcv_que => [08, 08 + rwnd)
// acked(nrcv_que) => be consumed by ikcp_recv
// rcv_nxt => ikcp_flush una => update sender's snd_una

// connected -> closed
class KCPSession final : public std::enable_shared_from_this<KCPSession> {
 public:
  struct Params {
    int snd_wnd{64};
    int rcv_wnd{128};
    int snd_hghwat{10};
    int nodelay{0};
    int interval{100};
    int resend{0};
    int nocongestion{0};
    int mtu{512};
    int head_room{0};
    int stream_mode{0};
  };

  explicit KCPSession(muduo::net::EventLoop* loop);

  ~KCPSession();

  // template <typename... Args>
  // static KCPSessionPtr Create(Args&&... params) {
  //   return std::shared_ptr<KCPSession>(
  //       new KCPSession(std::forward<Args>(params)...));
  // }

  bool Initialize(uint32_t session_id,
                  const muduo::net::InetAddress& peer_address,
                  const Params& params);

  void ProcessPacket(const KCPReceivedPacket& packet,
                     const muduo::net::InetAddress& peer_address);

  void Write(const void* data, size_t len);
  void Write(muduo::net::Buffer* buf);

  void Close(bool last_flush = false);

  void CloseAfterMs(uint32_t delay_ms, bool last_flush = false);

  bool IsClosed() const;

  muduo::net::EventLoop* loop() const { return loop_; }

  uint32_t session_id() const { return session_id_; }

  const muduo::net::InetAddress& peer_address() const { return peer_address_; }

  void set_connection_callback(ConnectionCallback cb) {
    connection_callback_ = std::move(cb);
  }

  void set_message_callback(MessageCallback cb) {
    message_callback_ = std::move(cb);
  }

  void set_write_complete_callback(WriteCompleteCallback cb) {
    write_complete_callback_ = std::move(cb);
  }

  void set_high_water_mark_callback(HighWaterMarkCallback cb) {
    high_water_mark_callback_ = std::move(cb);
  }

  void set_output_callback(OutputCallback cb) {
    output_callback_ = std::move(cb);
  }

  void set_flush_tx_queue(FlushTxQueueCallback cb) {
    flush_tx_queue_callback_ = std::move(cb);
  }

  void set_pending_error(PendingError error) { pending_error_ = error; }

 private:
  uint32_t CurrentMs() const;

  void OnConnectionEvent(bool connected);
  void OnReadEvent(size_t bytes_can_read);

  void UpdateConnectionState();
  void FlushTxQueue();

  // void ProcessPacketInLoopThread(const void* data, size_t len,
  //                                const muduo::net::InetAddress&
  //                                peer_address);

  void ProcessPacketInLoopThread(const KCPReceivedPacket& packet,
                                 const muduo::net::InetAddress& peer_address);
  void WriteInLoopThread(const void* data, size_t len);

  static int OnKCPOutput(char* buf, int len, IKCPCB* kcp, void* user);

  struct ScopedKCPCBDeleter {
    inline void operator()(IKCPCB* x) const {
      if (x != nullptr) {
        ikcp_release(x);
      }
    }
  };

  using ScopedKCPCB = std::unique_ptr<IKCPCB, ScopedKCPCBDeleter>;

  // eventloop
  muduo::net::EventLoop* const loop_{nullptr};

  // kcpcb
  ScopedKCPCB kcp_;

  // session id
  uint32_t session_id_{0};

  // water mark
  // size_t high_water_mark_{0};

  // peer address
  muduo::net::InetAddress peer_address_;

  // connection created time
  muduo::Timestamp base_time_;

  // update connection state timer
  muduo::net::TimerId state_timer_;

  // connection event callback
  ConnectionCallback connection_callback_;

  // write event callback
  WriteCompleteCallback write_complete_callback_;

  MessageCallback message_callback_;

  HighWaterMarkCallback high_water_mark_callback_;

  // CloseCallback close_callback_;

  OutputCallback output_callback_;
  FlushTxQueueCallback flush_tx_queue_callback_;

  muduo::net::Buffer input_buffer_;
  // muduo::net::Buffer output_buffer_;

  PendingError pending_error_;

  std::atomic<bool> closed_{false};
};

const KCPSession::Params ALLOW_UNUSED kNormalModeKCPParams = {
    .snd_wnd = 64,
    .rcv_wnd = 64,
    .snd_hghwat = 8,
    .nodelay = 0,
    .interval = 100,
    .resend = 3,
    .nocongestion = 0,
    .mtu = kDefaultMTUSize,
    .head_room = 0,
    .stream_mode = 0};

const KCPSession::Params ALLOW_UNUSED kFastModeKCPParams = {
    .snd_wnd = 128,
    .rcv_wnd = 128,
    .snd_hghwat = 16,
    .nodelay = 1,
    .interval = 50,
    .resend = 3,
    .nocongestion = 1,
    .mtu = kDefaultMTUSize,
    .head_room = 0,
    .stream_mode = 0};

#endif
