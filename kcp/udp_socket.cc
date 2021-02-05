
#include "udp_socket.h"

#include <net/if.h>

#include <muduo/base/Logging.h>
#include <muduo/net/InetAddress.h>

#include "log_util.h"

const socklen_t SockaddrStorage::kSockaddrInSize = sizeof(struct sockaddr_in);
const socklen_t SockaddrStorage::kSockaddrIn6Size = sizeof(struct sockaddr_in6);

SockaddrStorage::SockaddrStorage()
    : addr_len(sizeof(addr_storage)),
      addr(reinterpret_cast<struct sockaddr*>(&addr_storage)) {}

SockaddrStorage::SockaddrStorage(const SockaddrStorage& other)
    : addr_len(other.addr_len),
      addr(reinterpret_cast<struct sockaddr*>(&addr_storage)) {
  ::memcpy(addr, other.addr, addr_len);
}

SockaddrStorage& SockaddrStorage::operator=(const SockaddrStorage& other) {
  if (&other == this) {
    return *this;
  }

  addr_len = other.addr_len;
  ::memcpy(addr, other.addr, addr_len);

  return *this;
}

bool SockaddrStorage::ToSockAddr(const muduo::net::InetAddress& address,
                                 SockaddrStorage* storage) {
  assert(storage != nullptr);

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

bool SockaddrStorage::ToSockAddr(const struct sockaddr_storage& sock_addr,
                                 socklen_t sock_len, SockaddrStorage* storage) {
  assert(storage != nullptr);

  switch (sock_len) {
    case kSockaddrInSize:
    case kSockaddrIn6Size: {
      memcpy(storage->addr, &sock_addr, sock_len);
      storage->addr_len = sock_len;
      break;
    }

    default:
      return false;
  }

  return true;
}

bool SockaddrStorage::ToInetAddr(const SockaddrStorage& storage,
                                 muduo::net::InetAddress* address) {
  assert(address != nullptr);

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

bool SockaddrStorage::ToInetAddr(const struct sockaddr_storage& sock_addr,
                                 socklen_t sock_len,
                                 muduo::net::InetAddress* address) {
  assert(address != nullptr);

  switch (sock_len) {
    case kSockaddrInSize:
    case kSockaddrIn6Size: {
      const struct sockaddr_in6* addr =
          reinterpret_cast<const struct sockaddr_in6*>(&sock_addr);
      address->setSockAddrInet6(*addr);
      break;
    }

    default:
      return false;
  }

  return true;
}

const int UDPSocket::kInvalidSocket;

UDPSocket::UDPSocket() {}

UDPSocket::~UDPSocket() { Close(); }

int UDPSocket::Connect(const muduo::net::InetAddress& address) {
  assert(!IsValidSocket());
  assert(peer_address_.get() == nullptr);

  int addr_family = address.family();
  int rv = CreateSocket(addr_family);
  if (rv < 0) {
    return rv;
  }

  // IP_RECVERR
  rv = SetSocketOptions();
  if (rv < 0) {
    Close();
    return rv;
  }

  rv = ConnectImpl(address);
  if (rv < 0) {
    Close();
    return rv;
  }

  return rv;
}

int UDPSocket::Bind(const muduo::net::InetAddress& address) {
  assert(!IsValidSocket());

  int rv = CreateSocket(address.family());
  if (rv < 0) {
    return rv;
  }

  rv = SetSocketOptions();
  if (rv < 0) {
    Close();
    return rv;
  }

  rv = BindImpl(address);
  if (rv < 0) {
    Close();
    return rv;
  }

  return rv;
}

void UDPSocket::Close() {
  if (!IsValidSocket()) {
    return;
  }

  if (::close(sockfd_) < 0) {
    LOG_SYSERR << "::close";
  }

  sockfd_ = kInvalidSocket;
  addr_family_ = AF_UNSPEC;
  socket_options_ = 0;
}

void UDPSocket::AllowReuseAddress() {
  assert(!IsValidSocket());

  socket_options_ |= SOCKET_OPTION_REUSE_ADDRESS;
}

void UDPSocket::AllowReusePort() {
  assert(!IsValidSocket());

  socket_options_ |= SOCKET_OPTION_REUSE_PORT;
}

void UDPSocket::AllowBroadcast() {
  assert(!IsValidSocket());

  socket_options_ |= SOCKET_OPTION_BROADCAST;
}

void UDPSocket::AllowReceiveError() {
  assert(!IsValidSocket());

  socket_options_ |= SOCKET_OPTION_RECEIVE_ERROR;
}

void UDPSocket::AllowReceiveDSCPAndECN() {
  assert(!IsValidSocket());

  socket_options_ |= SOCKET_OPTION_RECEIVE_DSCP_AND_ECN;
}

int UDPSocket::GetLocalAddress(muduo::net::InetAddress* address) {
  assert(address != nullptr);

  if (!IsValidSocket()) {
    return -ENOTCONN;
  }

  if (local_address_.get() == nullptr) {
    SockaddrStorage storage;
    if (getsockname(sockfd_, storage.addr, &storage.addr_len) < 0) {
      return errno;
    }

    auto inet_addr = std::make_unique<muduo::net::InetAddress>();
    if (!SockaddrStorage::ToInetAddr(storage, inet_addr.get())) {
      return -EADDRNOTAVAIL;
    }
    local_address_ = std::move(inet_addr);
  }

  *address = *local_address_;
  return 0;
}

int UDPSocket::GetPeerAddress(muduo::net::InetAddress* address) {
  assert(address != nullptr);

  if (!IsValidSocket()) {
    return -ENOTCONN;
  }

  if (peer_address_.get() == nullptr) {
    SockaddrStorage storage;
    if (getpeername(sockfd_, storage.addr, &storage.addr_len) < 0) {
      return -errno;
    }

    auto inet_addr = std::make_unique<muduo::net::InetAddress>();
    if (!SockaddrStorage::ToInetAddr(storage, inet_addr.get())) {
      return -EADDRNOTAVAIL;
    }

    peer_address_ = std::move(inet_addr);
  }

  *address = *peer_address_;
  return 0;
}

int UDPSocket::ConnectImpl(const muduo::net::InetAddress& address) {
  assert(sockfd_ != kInvalidSocket);

  SockaddrStorage storage;
  if (!SockaddrStorage::ToSockAddr(address, &storage)) {
    return -EADDRNOTAVAIL;  // EFAULT;;
  }

  int rv = HANDLE_EINTR(::connect(sockfd_, storage.addr, storage.addr_len));
  int last_error = errno;
  if (rv < 0) {
    LOG_SYSERR << "::connect";
  }

  return rv == 0 ? 0 : -last_error;
}

int UDPSocket::BindImpl(const muduo::net::InetAddress& address) {
  assert(sockfd_ != kInvalidSocket);

  SockaddrStorage storage;
  if (!SockaddrStorage::ToSockAddr(address, &storage)) {
    return -EINVAL;
  }

  int rv = ::bind(sockfd_, storage.addr, storage.addr_len);
  int last_error = errno;
  if (rv < 0) {
    LOG_SYSERR << "::bind";
  }

  return rv == 0 ? 0 : -last_error;
}

int UDPSocket::CreateSocket(int addr_family) {
  assert(sockfd_ == kInvalidSocket);

  if (addr_family != AF_INET && addr_family != AF_INET6) {
    return -EAFNOSUPPORT;
  }

  int sockfd = ::socket(addr_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                        IPPROTO_UDP);
  if (sockfd == kInvalidSocket) {
    int last_error = errno;
    LOG_SYSERR << "::socket";
    return -last_error;
  }

  addr_family_ = addr_family;
  sockfd_ = sockfd;

  return 0;
}

int UDPSocket::SetSocketOptions() {
  assert(sockfd_ != kInvalidSocket);
  assert(addr_family_ != AF_UNSPEC);

  int true_value = 1;

  if (socket_options_ & SOCKET_OPTION_REUSE_ADDRESS) {
    ERROR_RETURN(::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &true_value,
                              sizeof(true_value)));
  }

  if (socket_options_ & SOCKET_OPTION_REUSE_PORT) {
    ERROR_RETURN(::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &true_value,
                              sizeof(true_value)));
  }

  if (socket_options_ & SOCKET_OPTION_BROADCAST) {
    ERROR_RETURN(::setsockopt(sockfd_, SOL_SOCKET, SO_BROADCAST, &true_value,
                              sizeof(true_value)));
  }

  if (socket_options_ & SOCKET_OPTION_RECEIVE_ERROR) {
    if (addr_family_ == AF_INET) {
      ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IP, IP_RECVERR, &true_value,
                                sizeof(true_value)));
    } else {
      ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IPV6, IPV6_RECVERR,
                                &true_value, sizeof(true_value)));
    }
  }

  if (socket_options_ & SOCKET_OPTION_RECEIVE_DSCP_AND_ECN) {
    if (addr_family_ == AF_INET) {
      ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IP, IP_RECVTOS, &true_value,
                                sizeof(true_value)));
    } else {
      ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IPV6, IPV6_RECVTCLASS,
                                &true_value, sizeof(true_value)));
    }
  }

  return 0;
}

int UDPSocket::SetReceiveBufferSize(int size) {
  assert(sockfd_ != kInvalidSocket);

  if (size < 0) {
    return -EINVAL;
  }

  ERROR_RETURN(
      ::setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)));

  return 0;
}

int UDPSocket::SetSendBufferSize(int size) {
  assert(sockfd_ != kInvalidSocket);

  if (size < 0) {
    return -EINVAL;
  }

  ERROR_RETURN(
      ::setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)));

  return 0;
}

int UDPSocket::SetMulticastIF(unsigned int ifindex) {
  assert(IsValidSocket());
  assert(addr_family_ != AF_UNSPEC);

  if (ifindex != 0) {
    char buf[IF_NAMESIZE];
    if (::if_indextoname(ifindex, buf) == nullptr) {
      return -errno;
    }
  }

  if (addr_family_ == AF_INET) {
    // man 7 ip
    struct ip_mreqn mreq;
    mreq.imr_ifindex = static_cast<int>(ifindex);
    mreq.imr_address.s_addr = htonl(INADDR_ANY);
    ERROR_RETURN(
        setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)));
  } else {
    u_int ifindex_value = static_cast<u_int>(ifindex);
    ERROR_RETURN(setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                            &ifindex_value, sizeof(ifindex_value)));
  }

  return 0;
}

int UDPSocket::SetMulticastIF(const char* ifname) {
  assert(IsValidSocket());
  assert(addr_family_ != AF_UNSPEC);

  // man 3 if_nametoindex
  unsigned int ifindex = ::if_nametoindex(ifname);
  if (ifindex == 0) {
    return -errno;
  }

  return SetMulticastIF(ifindex);
}

int UDPSocket::SetMulticastTTL(int ttl) {
  assert(IsValidSocket());
  assert(addr_family_ != AF_UNSPEC);

  if (addr_family_ == AF_INET) {
    if (ttl < 0 || ttl > 255) {
      return -EINVAL;
    }

    u_char ttl_value = static_cast<u_char>(ttl);
    ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl_value,
                              sizeof(ttl_value)));
  } else {
    if (ttl < -1 || ttl > 255) {
      return -EINVAL;
    }
    // man 7 ipv6
    // -1 in the value means use the default, otherwise it
    // should be between 0 and 255.
    int ttl_value = ttl;
    ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                              &ttl_value, sizeof(ttl_value)));
  }

  return 0;
}

int UDPSocket::SetMulticastLoop(bool loop) {
  assert(IsValidSocket());
  assert(addr_family_ != AF_UNSPEC);

  int on_off = loop ? 1 : 0;

  if (addr_family_ == AF_INET) {
    u_char loop_value = static_cast<u_char>(on_off);
    ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_LOOP,
                              &loop_value, sizeof(loop_value)));
  } else {
    u_int loop_value = static_cast<u_int>(on_off);
    ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
                              &loop_value, sizeof(loop_value)));
  }

  return 0;
}

int UDPSocket::SetDSCPAndECN(uint8_t dscp_and_ecn) {
  assert(IsValidSocket());
  assert(addr_family_ != AF_UNSPEC);

  // https://tools.ietf.org/html/rfc3168#section-5
  // 0     1     2     3     4     5     6     7
  // +-----+-----+-----+-----+-----+-----+-----+-----+
  // |          DS FIELD, DSCP           | ECN FIELD |
  // +-----+-----+-----+-----+-----+-----+-----+-----+
  int dscp_and_ecn_value = static_cast<int>(dscp_and_ecn);
  if (addr_family_ == AF_INET) {
    ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IP, IP_TOS, &dscp_and_ecn_value,
                              sizeof(dscp_and_ecn_value)));
  } else {
    ERROR_RETURN(::setsockopt(sockfd_, IPPROTO_IPV6, IPV6_TCLASS,
                              &dscp_and_ecn_value, sizeof(dscp_and_ecn_value)));
  }

  return 0;
}

int UDPSocket::JoinMulticastGroup(const muduo::net::InetAddress& group_address,
                                  unsigned int ifindex) {
  return JoinOrLeaveMulticastGroup(group_address, ifindex, true);
}

int UDPSocket::JoinMulticastGroup(const muduo::net::InetAddress& group_address,
                                  const char* ifname) {
  unsigned int ifindex = if_nametoindex(ifname);
  if (ifindex == 0) {
    return -errno;
  }

  return JoinOrLeaveMulticastGroup(group_address, ifindex, true);
}

int UDPSocket::LeaveMulticastGroup(const muduo::net::InetAddress& group_address,
                                   unsigned int ifindex) {
  return JoinOrLeaveMulticastGroup(group_address, ifindex, false);
}

int UDPSocket::LeaveMulticastGroup(const muduo::net::InetAddress& group_address,
                                   const char* ifname) {
  unsigned int ifindex = if_nametoindex(ifname);
  if (ifindex == 0) {
    return -errno;
  }

  return JoinOrLeaveMulticastGroup(group_address, ifindex, false);
}

int UDPSocket::JoinOrLeaveMulticastGroup(
    const muduo::net::InetAddress& group_address, unsigned int ifindex,
    bool op_join) {
  assert(IsValidSocket());

  if (group_address.family() != addr_family_) {
    return -EINVAL;
  }

  if (!IsAddressMulticast(group_address)) {
    return -EINVAL;
  }

  if (ifindex > 0) {
    char ifname[IF_NAMESIZE];
    if (::if_indextoname(ifindex, ifname) == nullptr) {
      return -errno;
    }
  }

  struct group_req req;
  req.gr_interface = ifindex;

  int level = IPPROTO_IP;
  if (group_address.family() == AF_INET) {
    level = IPPROTO_IP;
    memcpy(&req.gr_group, group_address.getSockAddr(),
           SockaddrStorage::kSockaddrInSize);
  } else if (group_address.family() == AF_INET6) {
    level = IPPROTO_IPV6;
    memcpy(&req.gr_group, group_address.getSockAddr(),
           SockaddrStorage::kSockaddrIn6Size);
  } else {
    return -EINVAL;
  }

  int optname = op_join ? MCAST_JOIN_GROUP : MCAST_LEAVE_GROUP;
  ERROR_RETURN(::setsockopt(sockfd_, level, optname, &req, sizeof(req)));

  return 0;
}

int UDPSocket::Read(void* buf, size_t len) {
  assert(buf != nullptr);
  return RecvFrom(buf, len, nullptr, 0);
}

int UDPSocket::RecvFrom(void* buf, size_t len, muduo::net::InetAddress* address,
                        int flags) {
  assert(buf != nullptr);

  SockaddrStorage storage;
  // struct sockaddr* addr = storage.addr;
  // socklen_t* addr_len = &storage.addr_len;
  // if (address == nullptr) {
  //  addr = nullptr;
  //  addr_len = nullptr;
  // }

  // ssize_t => int
  int bytes_recvd = static_cast<int>(HANDLE_EINTR(
      ::recvfrom(sockfd_, buf, len, flags, storage.addr, &storage.addr_len)));

  int result;
  if (bytes_recvd >= 0) {
    if (address != nullptr && !SockaddrStorage::ToInetAddr(storage, address)) {
      result = -EINVAL;
    } else {
      result = bytes_recvd;
    }
  } else {
    int last_error = errno;
    if (!IS_EAGAIN(last_error)) {
      LOG_SYSERR << "::recvfrom";
    }
    result = -last_error;
  }

  return result;
}

int UDPSocket::Write(const void* buf, size_t len) {
  assert(buf != nullptr);
  return SendToImpl(buf, len, nullptr, 0);
}

int UDPSocket::SendTo(const void* buf, size_t len,
                      const muduo::net::InetAddress& address, int flags) {
  assert(buf != nullptr);
  return SendToImpl(buf, len, &address, flags);
}

int UDPSocket::SendToImpl(const void* buf, size_t len,
                          const muduo::net::InetAddress* address, int flags) {
  SockaddrStorage storage;
  struct sockaddr* addr = storage.addr;
  if (address == nullptr) {
    addr = nullptr;
    storage.addr_len = 0;
  } else if (!SockaddrStorage::ToSockAddr(*address, &storage)) {
    return -EFAULT;
  }

  int result = static_cast<int>(
      HANDLE_EINTR(::sendto(sockfd_, buf, len, flags, addr, storage.addr_len)));
  if (result < 0) {
    int last_error = errno;
    if (!IS_EAGAIN(last_error)) {
      LOG_SYSERR << "::sendto";
    }

    return -last_error;
  }

  return result;
}

int UDPSocket::RecvMmsg(struct mmsghdr* msgvec, unsigned int vlen, int flags) {
  assert(msgvec != nullptr);
  assert(IsValidSocket());

  int result = HANDLE_EINTR(::recvmmsg(sockfd_, msgvec, vlen, flags, nullptr));

  if (result < 0) {
    int last_error = errno;
    if (!IS_EAGAIN(last_error)) {
      LOG_SYSERR << "::recvmmsg";
    }
    result = -last_error;
  }

  return result;
}

int UDPSocket::SendMmsg(struct mmsghdr* msgvec, unsigned int vlen, int flags) {
  assert(msgvec != nullptr);
  assert(IsValidSocket());

  int result = HANDLE_EINTR(::sendmmsg(sockfd_, msgvec, vlen, flags));

  if (result < 0) {
    int last_error = errno;
    if (!IS_EAGAIN(last_error)) {
      LOG_SYSERR << "::sendmmsg";
    }
    result = -last_error;
  }

  return result;
}

int UDPSocket::SendMsg(const struct msghdr* msg, int flags) {
  assert(msg != nullptr);
  assert(IsValidSocket());

  int result = static_cast<int>(HANDLE_EINTR(::sendmsg(sockfd_, msg, flags)));

  if (result < 0) {
    int last_error = errno;
    if (!IS_EAGAIN(last_error)) {
      LOG_SYSERR << "::sendmsg";
    }
    result = -last_error;
  }

  return result;
}

int UDPSocket::RecvMsg(struct msghdr* msg, int flags) {
  assert(msg != nullptr);
  assert(IsValidSocket());

  int result = static_cast<int>(HANDLE_EINTR(::recvmsg(sockfd_, msg, flags)));

  if (result < 0) {
    int last_error = errno;
    if (!IS_EAGAIN(last_error)) {
      LOG_SYSERR << "::recvmsg";
    }
    result = -last_error;
  }

  return result;
}

bool UDPSocket::IsAddressMulticast(const muduo::net::InetAddress& address) {
  if (address.family() == AF_INET) {
    const struct sockaddr_in* addr =
        reinterpret_cast<const struct sockaddr_in*>(address.getSockAddr());
    return IN_MULTICAST(ntohl(addr->sin_addr.s_addr));
  } else if (address.family() == AF_INET6) {
    const struct sockaddr_in6* addr =
        reinterpret_cast<const struct sockaddr_in6*>(address.getSockAddr());
    return IN6_IS_ADDR_MULTICAST(addr->sin6_addr.s6_addr);
  }

  return false;
}
