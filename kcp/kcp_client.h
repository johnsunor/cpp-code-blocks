
#ifndef KCP_CLIENT_H_
#define KCP_CLIENT_H_

#include <stdint.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <muduo/base/Timestamp.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TimerId.h>

#include "common/macros.h"

#include "kcp_callbacks.h"
#include "kcp_packets.h"

namespace muduo {
namespace net {

class Buffer;
class Channel;
class EventLoop;
class EventLoopThreadPool;
}  // namespace net
}  // namespace muduo

class UDPSocket;
struct KCPPendingSession;

class KCPClient final {
 public:
  // closed -> pending -> connected -> closed
  enum State : uint8_t {
    CLOSED,
    PENDING,
    CONNECTED,
  };

  explicit KCPClient(muduo::net::EventLoop* loop);

  ~KCPClient();

  int Connect(const muduo::net::InetAddress& address);

  void ConnectOrDie(const muduo::net::InetAddress& address);

  void Disconnect();

  void set_connection_callback(ConnectionCallback cb) {
    connection_callback_ = std::move(cb);
  }

  void set_message_callback(MessageCallback cb) {
    message_callback_ = std::move(cb);
  }

  void set_write_complete_callback(WriteCompleteCallback cb) {
    write_complete_callback_ = std::move(cb);
  }

  void set_error_message_callback(ErrorMessageCallback cb) {
    error_message_callback_ = std::move(cb);
  }

  State state() const { return state_; }

  bool reconnect_enabled() const { return reconnect_enabled_; }
  void set_reconnect_enabled(bool enabled) { reconnect_enabled_ = enabled; }

 private:
  void set_state(State state) { state_ = state; }

  // read/write/error event handler
  void HandleRead(muduo::Timestamp receive_time);
  void HandleWrite();
  void HandleError();

  void BuildSession();
  void ResetSession();
  void Reconnect();
  void RunPeriodicTask();

  bool InitializeSession(KCPSessionPtr& session, uint32_t session_id);

  void ProcessPacket(KCPReceivedPacket& packet);

  void ProcessSynPacket(const KCPPublicHeader& public_header,
                        KCPReceivedPacket& packet);
  void ProcessRstPacket(const KCPPublicHeader& public_header,
                        KCPReceivedPacket& packet);
  void ProcessPongPacket(const KCPPublicHeader& public_header,
                         KCPReceivedPacket& packet);
  void ProcessDataPacket(const KCPPublicHeader& public_header,
                         KCPReceivedPacket& packet);

  void SendPacket(uint8_t packet_type, uint32_t session_id);
  void SendDataToWire(const KCPPendingSendPacket& packet,
                      const muduo::net::InetAddress& address);

  muduo::net::EventLoop* const loop_{nullptr};
  muduo::net::InetAddress client_address_;
  muduo::net::InetAddress server_address_;

  // client side socket
  std::unique_ptr<UDPSocket> socket_;
  std::unique_ptr<muduo::net::Channel> channel_;

  std::unique_ptr<KCPPendingSession> pending_session_;
  KCPSessionPtr session_;

  muduo::net::TimerId periodic_task_timer_;
  muduo::Timestamp last_received_time_;
  muduo::Timestamp last_ping_time_;

  State state_{CLOSED};

  bool reconnect_enabled_{false};
  bool reconnect_timer_registered_{false};
  int reconnect_times_{0};
  int async_error_times_{0};
  muduo::net::TimerId reconnect_timer_;

  ConnectionCallback connection_callback_;

  MessageCallback message_callback_;

  WriteCompleteCallback write_complete_callback_;

  ErrorMessageCallback error_message_callback_;

  DISALLOW_COPY_AND_ASSIGN(KCPClient);
};

#endif
