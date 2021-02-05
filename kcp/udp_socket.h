
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

  void AllowReuseAddress();
  void AllowReusePort();
  void AllowBroadcast();
  void AllowReceiveError();
  void AllowReceiveDSCPAndECN();

  int SetReceiveBufferSize(int size);
  int SetSendBufferSize(int size);

  // for sending mcast datagram
  int SetMulticastIF(unsigned int ifindex);
  int SetMulticastIF(const char* ifname);
  int SetMulticastTTL(int ttl);
  int SetMulticastLoop(bool loop);

  int SetDSCPAndECN(uint8_t dscp_and_ecn);

  // for receiving mcast datagram
  int JoinMulticastGroup(const muduo::net::InetAddress& group_address,
                         const char* ifname);
  int JoinMulticastGroup(const muduo::net::InetAddress& group_address,
                         unsigned int ifindex);
  int LeaveMulticastGroup(const muduo::net::InetAddress& group_address,
                          const char* ifname);
  int LeaveMulticastGroup(const muduo::net::InetAddress& group_address,
                          unsigned int ifindex);

  int sockfd() const { return sockfd_; }
  bool IsValidSocket() const { return sockfd_ != kInvalidSocket; }

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

  static bool IsAddressMulticast(const muduo::net::InetAddress& address);

 private:
  enum SocketOptions : uint8_t {
    // SOL_SOCKET
    SOCKET_OPTION_REUSE_ADDRESS = 1 << 0,
    SOCKET_OPTION_REUSE_PORT = 1 << 1,
    SOCKET_OPTION_BROADCAST = 1 << 2,

    // IPPROTO_IP/IPV6
    SOCKET_OPTION_RECEIVE_ERROR = 1 << 3,
    SOCKET_OPTION_RECEIVE_DSCP_AND_ECN = 1 << 4,
  };

  int CreateSocket(int addr_family);
  int SetSocketOptions();

  int ConnectImpl(const muduo::net::InetAddress& address);
  int BindImpl(const muduo::net::InetAddress& address);
  int SendToImpl(const void* buf, size_t len,
                 const muduo::net::InetAddress* address, int flags);

  int JoinOrLeaveMulticastGroup(const muduo::net::InetAddress& group_address,
                                unsigned int ifindex, bool op_join);

  int sockfd_{kInvalidSocket};
  int addr_family_{AF_UNSPEC};
  int socket_options_{0};

  std::unique_ptr<muduo::net::InetAddress> local_address_;
  std::unique_ptr<muduo::net::InetAddress> peer_address_;

  static const int kInvalidSocket = -1;

  DISALLOW_COPY_AND_ASSIGN(UDPSocket);
};

#endif
