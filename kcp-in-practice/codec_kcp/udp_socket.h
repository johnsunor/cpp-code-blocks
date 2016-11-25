
#include <assert.h>

#include <map>
#include <queue>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <boost/bind.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include <muduo/net/InetAddress.h>

#include "common/macros.h"

const socklen_t kSockaddrInSize = sizeof(struct sockaddr_in);
const socklen_t kSockaddrIn6Size = sizeof(struct sockaddr_in6);

const int kInvalidSocket = -1;

const int kDefaultMaxPacketSize = 1452;

struct SockaddrStorage {
  SockaddrStorage()
      : addr_len(sizeof(addr_storage)),
        addr(reinterpret_cast<struct sockaddr*>(&addr_storage)) {}

  SockaddrStorage(const SockaddrStorage& other)
      : addr_len(other.addr_len),
        addr(reinterpret_cast<struct sockaddr*>(&addr_storage)) {
    ::memcpy(addr, other.addr, addr_len);
  }

  void operator=(const SockaddrStorage& other) {
    addr_len = other.addr_len;
    ::memcpy(addr, other.addr, addr_len);
  }

  static bool ToSockAddr(const muduo::net::InetAddress& address,
                         SockaddrStorage* storage) {
    assert(storage != NULL);

    int addr_family = address.family();
    switch (addr_family) {
      case AF_INET: {
        const struct sockaddr_in* addr =
            reinterpret_cast<const struct sockaddr_in*>(address.getSockAddr());
        memcpy(storage->addr, addr, kSockaddrInSize);
        storage->addr_len = kSockaddrInSize;
        break;
      }

      case AF_INET6: {
        const struct sockaddr_in6* addr =
            reinterpret_cast<const struct sockaddr_in6*>(address.getSockAddr());
        memcpy(storage->addr, addr, kSockaddrIn6Size);
        storage->addr_len = kSockaddrIn6Size;
        break;
      }

      default:
        return false;
    }

    return true;
  }

  static bool ToInetAddr(const SockaddrStorage& storage,
                         muduo::net::InetAddress* address) {
    assert(address != NULL);

    int addr_len = storage.addr_len;
    switch (addr_len) {
      case kSockaddrInSize:
      case kSockaddrIn6Size: {
        const struct sockaddr_in6* addr =
            reinterpret_cast<const struct sockaddr_in6*>(storage.addr);
        address->setSockAddrInet6(*addr);
        break;
      }

      default:
        return false;
    }

    return true;
  }

  struct sockaddr_storage addr_storage;
  socklen_t addr_len;
  struct sockaddr* const addr;
};

class UDPSocket : boost::noncopyable {
 public:
  UDPSocket();

  int Connect(const muduo::net::InetAddress& address);
  int Bind(const muduo::net::InetAddress& address);
  void Close();

  int GetPeerAddress(muduo::net::InetAddress* address) const;
  int GetLocalAddress(muduo::net::InetAddress* address) const;

  int Read(void* buf, size_t len);
  int Write(const void* buf, size_t len);

  int RecvFrom(void* buf, size_t len, muduo::net::InetAddress* address);

  int SendTo(const void* buf, size_t len,
             const muduo::net::InetAddress& address);
  int SendToOrWrite(const void* buf, size_t len,
                    const muduo::net::InetAddress* address);

  void AllowAddressReuse();
  void AllowBroadcast();

  // Set the receive buffer size (in bytes) for the socket.
  int SetReceiveBufferSize(int32_t size);

  // Set the send buffer size (in bytes) for the socket.
  int SetSendBufferSize(int32_t size);

  int SetMulticastInterface(uint32_t interface_index);

  int SetMulticastTimeToLive(int time_to_live);

  int sockfd() const { return sockfd_; }

  bool is_connected() const { return sockfd_ != kInvalidSocket; }

 private:
  enum SocketOptions {
    SOCKET_OPTION_REUSE_ADDRESS = 1 << 0,
    SOCKET_OPTION_REUSE_PORT = 1 << 1,
    SOCKET_OPTION_BROADCAST = 1 << 2,
    SOCKET_OPTION_MULTICAST_LOOP = 1 << 3
  };

  int CreateSocket(int addr_family);
  int SetSocketOptions();
  int DoBind(const muduo::net::InetAddress& address);
  int InternalSendTo(const void* buf, size_t len,
                     const muduo::net::InetAddress* address);

  int sockfd_;
  int addr_family_;
  int socket_options_;

  uint32_t multicast_interface_;

  // Multicast socket options cached for SetSocketOption.
  // Cannot be used after Bind().
  int multicast_time_to_live_;

  mutable boost::scoped_ptr<muduo::net::InetAddress> local_address_;
  mutable boost::scoped_ptr<muduo::net::InetAddress> remote_address_;

  // boost::scoped_ptr<muduo::net::InetAddress> send_to_address_;
};
