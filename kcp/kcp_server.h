
#ifndef KCP_SERVER_H_
#define KCP_SERVER_H_

#include <stdint.h>

#include <memory>
#include <unordered_map>

#include <muduo/base/Timestamp.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TimerId.h>

#include "common/macros.h"

#include "kcp_callbacks.h"
#include "kcp_constants.h"
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

class KCPServer final {
 public:
  explicit KCPServer(muduo::net::EventLoop* loop);

  ~KCPServer();

  int Listen(const muduo::net::InetAddress& address);

  void ListenOrDie(const muduo::net::InetAddress& address);

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

  void set_num_threads(uint8_t num_threads) { num_threads_ = num_threads; }

  bool IsWriteBlocked() const { return write_blocked_; }

 private:
  void Initialize();

  void RunPeriodicTask();

  void HandleRead(muduo::Timestamp receive_time);

  void HandleWrite();

  void HandleError();

  bool GenerateSessionId(uint32_t* session_id) const;

  bool InitializeSession(KCPSessionPtr& session, uint32_t session_id,
                         const muduo::net::InetAddress& client_address);

  void SendPacket(uint8_t packet_type, uint32_t session_id,
                  const muduo::net::InetAddress& client_address);

  void ProcessSynPacket(const KCPPublicHeader& public_header,
                        KCPReceivedPacket& packet,
                        const muduo::net::InetAddress& client_address);
  void ProcessAckPacket(const KCPPublicHeader& public_header,
                        KCPReceivedPacket& packet,
                        const muduo::net::InetAddress& client_address);
  void ProcessPingPacket(const KCPPublicHeader& public_header,
                         KCPReceivedPacket& packet,
                         const muduo::net::InetAddress& client_address);
  void ProcessRstPacket(const KCPPublicHeader& public_header,
                        KCPReceivedPacket& packet,
                        const muduo::net::InetAddress& client_address);
  void ProcessDataPacket(const KCPPublicHeader& public_header,
                         KCPReceivedPacket& packet,
                         const muduo::net::InetAddress& client_address);
  void ProcessPacket(KCPReceivedPacket& packet,
                     const muduo::net::InetAddress& client_address);

  void SetWritable() { write_blocked_ = false; }
  void SetWriteBlocked() { write_blocked_ = true; }

  void InitializeThread(muduo::net::EventLoop* loop) const;
  void AppendPacket(const KCPPendingSendPacket& packet,
                    const muduo::net::InetAddress& address);
  void FlushTxQueue();

  struct RawPacket {
    struct iovec iov;
    struct sockaddr_storage addr;
    // MSG_TRUNC
    char buf[kMaxPacketSize + 1];
  };

  struct ThreadData {
    std::unique_ptr<mmsghdr[]> mmsg_hdrs;
    std::unique_ptr<RawPacket[]> raw_packets;
    unsigned int num_packets{0};
  };

  muduo::net::EventLoop* const loop_{nullptr};
  muduo::net::InetAddress server_address_;

  // server side socket
  std::unique_ptr<UDPSocket> socket_;
  std::unique_ptr<muduo::net::Channel> channel_;

  // dispatch session to different threads
  uint8_t num_threads_{0};
  std::unique_ptr<muduo::net::EventLoopThreadPool> thread_pool_;

  std::unique_ptr<mmsghdr[]> mmsg_hdrs_;
  std::unique_ptr<RawPacket[]> raw_packets_;

  using PendingSessionMap =
      std::unordered_map<std::string, std::unique_ptr<KCPPendingSession>>;
  using SessionMap = std::unordered_map<uint32_t, KCPSessionPtr>;
  using IdleSessionMap = std::unordered_map<uint32_t, muduo::Timestamp>;

  PendingSessionMap pending_session_map_;
  SessionMap session_map_;
  IdleSessionMap idle_session_map_;

  muduo::net::TimerId periodic_task_timer_;

  bool write_blocked_{false};
  // std::vector<std::unique_ptr<RawPacket>> queued_packets_;

  std::unordered_map<uint32_t, muduo::Timestamp> time_wait_session_map_;

  ConnectionCallback connection_callback_;

  MessageCallback message_callback_;

  WriteCompleteCallback write_complete_callback_;

  HighWaterMarkCallback high_water_mark_callback_;

  DISALLOW_COPY_AND_ASSIGN(KCPServer);
};

#endif
