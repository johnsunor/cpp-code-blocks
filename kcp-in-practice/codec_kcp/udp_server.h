
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/ptr_container/ptr_list.hpp>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include "udp_socket.h"

// class UDPSocket;
// class EventLoop;
// class Channel;

class UDPServer : boost::noncopyable {
 public:
  typedef boost::function<void(muduo::net::Buffer*, muduo::Timestamp,
                               const muduo::net::InetAddress&)> MessageCallback;

  explicit UDPServer(muduo::net::EventLoop* loop)
      : loop_(CHECK_NOTNULL(loop)),
        max_packet_size_(kDefaultMaxPacketSize),
        read_buf_(kDefaultMaxPacketSize),
        write_blocked_(false) {}

  UDPServer(muduo::net::EventLoop* loop, muduo::net::InetAddress listen_addr)
      : loop_(CHECK_NOTNULL(loop)),
        max_packet_size_(kDefaultMaxPacketSize),
        read_buf_(kDefaultMaxPacketSize),
        write_blocked_(false) {
    ListenOrDie(listen_addr);
  }

  int Listen(const muduo::net::InetAddress& address) {
    return socket_.Bind(address);
  }

  void ListenOrDie(const muduo::net::InetAddress& address) {
    int result = socket_.Bind(address);
    if (result < 0) {
      LOG_SYSFATAL << "UDPServer::ListenOrDie";
    }
  }

  int GetPeerAddress(muduo::net::InetAddress* address) const {
    return socket_.GetPeerAddress(address);
  }

  int GetLocalAddress(muduo::net::InetAddress* address) const {
    return socket_.GetLocalAddress(address);
  }

  void Close() { socket_.Close(); }

  void Start();

  void HandleRead(muduo::Timestamp receive_time);

  void HandleWrite();

  void HandleError();

  int SendTo(const void* buf, size_t len,
             const muduo::net::InetAddress& address);

  int SendTo(muduo::net::Buffer* buf, const muduo::net::InetAddress& address);

  void SendOrQueuePacket(const void* buf, size_t len,
                         const muduo::net::InetAddress& address);

  void set_message_callback(const MessageCallback& cb) {
    message_callback_ = cb;
  }

  size_t max_packet_size() const { return max_packet_size_; };

  void set_max_packet_size(size_t max_packet_size) {
    max_packet_size_ = max_packet_size;
    read_buf_.ensureWritableBytes(max_packet_size_);
  }

  bool IsWriteBlocked() const { return write_blocked_; }

  void SetWritable() { write_blocked_ = false; }

  void SetWriteBlocked() { write_blocked_ = true; }

  struct QueuedPacket {
    QueuedPacket(const void* buf, size_t len,
                 const muduo::net::InetAddress& addr)
        : data(new char[len]), size(len), peer_addr(addr) {
      ::memcpy(data, buf, len);
    }

    ~QueuedPacket() {
      delete[] data;
      data = NULL;
    }

    char* data;
    const size_t size;
    const muduo::net::InetAddress peer_addr;
  };

 private:
  muduo::net::EventLoop* const loop_;

  // server side socket
  UDPSocket socket_;
  boost::scoped_ptr<muduo::net::Channel> channel_;

  size_t max_packet_size_;
  muduo::net::Buffer read_buf_;

  MessageCallback message_callback_;

  bool write_blocked_;

  boost::ptr_list<QueuedPacket> queued_packets_;
};
