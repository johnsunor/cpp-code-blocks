
#ifndef UDP_SOCKET_H_
#define UDP_SOCKET_H_

#include <netinet/in.h>

#include <memory>

#include "common/macros.h"

namespace muduo {
namespace net {

class InetAddress;
};
};  // namespace muduo

struct SockaddrStorage {
  SockaddrStorage();
  SockaddrStorage(const SockaddrStorage& other);
  ~SockaddrStorage() = default;

  SockaddrStorage& operator=(const SockaddrStorage& other);

  static bool ToSockAddr(const muduo::net::InetAddress& address,
                         SockaddrStorage* storage);
  static bool ToSockAddr(const struct sockaddr_storage& sock_addr,
                         socklen_t sock_len, SockaddrStorage* storage);
  static bool ToInetAddr(const SockaddrStorage& storage,
                         muduo::net::InetAddress* address);
  static bool ToInetAddr(const struct sockaddr_storage& sock_addr,
                         socklen_t sock_len, muduo::net::InetAddress* address);

  socklen_t addr_len;
  struct sockaddr_storage addr_storage;
  struct sockaddr* const addr;

  static const socklen_t kSockaddrInSize;
  static const socklen_t kSockaddrIn6Size;
};

class UDPSocket final {
 public:
  UDPSocket();
  ~UDPSocket();

  int Connect(const muduo::net::InetAddress& address);
  int Bind(const muduo::net::InetAddress& address);
  void Close();

  int GetPeerAddress(muduo::net::InetAddress* address);
  int GetLocalAddress(muduo::net::InetAddress* address);

  int Read(void* buf, size_t len);
  int Write(const void* buf, size_t len);

  int RecvFrom(void* buf, size_t len, muduo::net::InetAddress* address,
               int flags = 0);
  int SendTo(const void* buf, size_t len,
             const muduo::net::InetAddress& address, int flags = 0);

  int RecvMsg(struct msghdr* msg, int flags = 0);
  int SendMsg(const struct msghdr* msg, int flags = 0);

  int RecvMmsg(struct mmsghdr* msgvec, unsigned int vlen, int flags = 0);
  int SendMmsg(struct mmsghdr* msgvec, unsigned int vlen, int flags = 0);

  void AllowReuseAddress();
  void AllowReusePort();
  void AllowBroadcast();
  void AllowReceiveError();

  int SetReceiveBufferSize(int32_t size);

  int SetSendBufferSize(int32_t size);

  int SetMulticastInterface(uint32_t interface_index);

  int SetMulticastTimeToLive(int time_to_live);

  int sockfd() const { return sockfd_; }

  bool IsValidSocket() const { return sockfd_ != kInvalidSocket; }

 private:
  enum SocketOptions : uint8_t {
    SOCKET_OPTION_REUSE_ADDRESS = 1 << 0,
    SOCKET_OPTION_REUSE_PORT = 1 << 1,
    SOCKET_OPTION_BROADCAST = 1 << 2,
    SOCKET_OPTION_RECEIVE_ERROR = 1 << 3,
    SOCKET_OPTION_MULTICAST_LOOP = 1 << 4
  };

  int CreateSocket(int addr_family);
  int SetSocketOptions();

  int ConnectImpl(const muduo::net::InetAddress& address);
  int BindImpl(const muduo::net::InetAddress& address);
  int SendToImpl(const void* buf, size_t len,
                 const muduo::net::InetAddress* address, int flags);

  int sockfd_{kInvalidSocket};
  int addr_family_{AF_UNSPEC};
  int socket_options_{SOCKET_OPTION_MULTICAST_LOOP};

  uint32_t multicast_interface_{0};
  int multicast_time_to_live_{IP_DEFAULT_MULTICAST_TTL};

  std::unique_ptr<muduo::net::InetAddress> local_address_;
  std::unique_ptr<muduo::net::InetAddress> peer_address_;

  static const int kInvalidSocket = -1;

  DISALLOW_COPY_AND_ASSIGN(UDPSocket);
};

#endif
