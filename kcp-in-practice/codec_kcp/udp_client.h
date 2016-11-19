
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/ptr_container/ptr_list.hpp>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include "udp_socket.h"

// class UDPSocket;
// class EventLoop;
// class Channel;

class UDPClient : boost::noncopyable {
 public:
  typedef boost::function<void(muduo::net::Buffer*, muduo::Timestamp)>
      MessageCallback;

  explicit UDPClient(muduo::net::EventLoop* loop)
      : loop_(CHECK_NOTNULL(loop)),
        max_packet_size_(kDefaultMaxPacketSize),
        read_buf_(kDefaultMaxPacketSize),
        write_blocked_(false) {}

  int Connect(const muduo::net::InetAddress& address);

  void Close() { socket_.Close(); }

  void Start();

  void HandleRead(muduo::Timestamp receive_time);

  void HandleWrite();

  void HandleError();

  int Write(const void* buf, size_t len);

  int Write(muduo::net::Buffer* buf);

  int WriteOrQueuePcket(const void* buf, size_t len);

  void set_message_callback(const MessageCallback& cb) {
    message_callback_ = cb;
  }

  size_t max_packet_size() const { return max_packet_size_; };

  void set_max_packet_size(size_t max_packet_size) {
    max_packet_size_ = max_packet_size;
    read_buf_.ensureWritableBytes(max_packet_size_);
  }

  int GetPeerAddress(muduo::net::InetAddress* address) const {
    return socket_.GetPeerAddress(address);
  }

  int GetLocalAddress(muduo::net::InetAddress* address) const {
    return socket_.GetLocalAddress(address);
  }

  bool IsWriteBlocked() const { return write_blocked_; }

  void SetWritable() { write_blocked_ = false; }

  void SetWriteBlocked() { write_blocked_ = true; }

  struct QueuedPacket {
    QueuedPacket(const void* buf, size_t len) : data(new char[len]), size(len) {
      ::memcpy(data, buf, len);
    }

    ~QueuedPacket() {
      delete[] data;
      data = NULL;
    }

    char* data;
    const size_t size;
  };

 private:
  muduo::net::EventLoop* const loop_;

  // client side socket
  UDPSocket socket_;
  boost::scoped_ptr<muduo::net::Channel> channel_;

  size_t max_packet_size_;
  muduo::net::Buffer read_buf_;

  MessageCallback message_callback_;

  bool write_blocked_;

  boost::ptr_list<QueuedPacket> queued_packets_;
};
