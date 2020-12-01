
#include "udp_socket.h"

#include <memory>

#include <muduo/base/Logging.h>
#include <muduo/net/InetAddress.h>

#include "common/macros.h"

const socklen_t SockaddrStorage::kSockaddrInSize = sizeof(struct sockaddr_in);
const socklen_t SockaddrStorage::kSockaddrIn6Size = sizeof(struct sockaddr_in6);

// static_assert(sizeof(struct sockaddr_storage) >= sizeof(struct sockaddr_in),
//              "sockaddr_storage size greater than sockaddr_in size");
// static_assert(sizeof(struct sockaddr_storage) >= sizeof(struct sockaddr_in6),
//              "sockaddr_storage size greater than sockaddr_in6 size");

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

bool SockaddrStorage::ToSockAddr(const struct sockaddr_storage& sock_addr,
                                 socklen_t sock_len, SockaddrStorage* storage) {
  assert(storage != NULL);

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

bool SockaddrStorage::ToInetAddr(const struct sockaddr_storage& sock_addr,
                                 socklen_t sock_len,
                                 muduo::net::InetAddress* address) {
  assert(address != NULL);

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
  socket_options_ = SOCKET_OPTION_MULTICAST_LOOP;
  multicast_interface_ = 0;
  multicast_time_to_live_ = IP_DEFAULT_MULTICAST_TTL;
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

int UDPSocket::SetMulticastInterface(uint32_t interface_index) {
  assert(!IsValidSocket());

  multicast_interface_ = interface_index;
  return 0;
}

int UDPSocket::SetMulticastTimeToLive(int time_to_live) {
  assert(!IsValidSocket());

  if (time_to_live < 0 || time_to_live > 255) {
    return -EINVAL;
  }

  multicast_time_to_live_ = time_to_live;
  return 0;
}

int UDPSocket::SetSocketOptions() {
  assert(sockfd_ != kInvalidSocket);
  assert(addr_family_ != AF_UNSPEC);

  int true_value = 1;
  if (socket_options_ & SOCKET_OPTION_REUSE_ADDRESS) {
    int rv = ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &true_value,
                          sizeof(true_value));
    if (rv < 0) {
      return -errno;
    }
  }

  if (socket_options_ & SOCKET_OPTION_REUSE_PORT) {
    int rv = ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &true_value,
                          sizeof(true_value));
    if (rv < 0) {
      return -errno;
    }
  }

  if (socket_options_ & SOCKET_OPTION_BROADCAST) {
    int rv = ::setsockopt(sockfd_, SOL_SOCKET, SO_BROADCAST, &true_value,
                          sizeof(true_value));
    if (rv < 0) {
      return -errno;
    }
  }

  if (socket_options_ & SOCKET_OPTION_RECEIVE_ERROR) {
    int rv;
    if (addr_family_ == AF_INET) {
      rv = ::setsockopt(sockfd_, IPPROTO_IP, IP_RECVERR, &true_value,
                        sizeof(true_value));
    } else {
      rv = ::setsockopt(sockfd_, IPPROTO_IPV6, IPV6_RECVERR, &true_value,
                        sizeof(true_value));
    }
    if (rv < 0) {
      return -errno;
    }
  }

  if (!(socket_options_ & SOCKET_OPTION_MULTICAST_LOOP)) {
    int rv;
    if (addr_family_ == AF_INET) {
      u_char loop = 0;
      rv = ::setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
                        sizeof(loop));
    } else {
      u_int loop = 0;
      rv = ::setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop,
                        sizeof(loop));
    }
    if (rv < 0) {
      return -errno;
    }
  }

  if (multicast_time_to_live_ != IP_DEFAULT_MULTICAST_TTL) {
    int rv;
    if (addr_family_ == AF_INET) {
      u_char ttl = static_cast<u_char>(multicast_time_to_live_);
      rv = ::setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                        sizeof(ttl));
    } else {
      // Signed integer. -1 to use route default.
      int ttl = multicast_time_to_live_;
      rv = ::setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl,
                        sizeof(ttl));
    }
    if (rv < 0) {
      return -errno;
    }
  }

  if (multicast_interface_ != 0) {
    if (addr_family_ == AF_INET) {
      ip_mreqn mreq;
      mreq.imr_ifindex = multicast_interface_;
      mreq.imr_address.s_addr = htonl(INADDR_ANY);
      int rv = setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_IF,
                          reinterpret_cast<const char*>(&mreq), sizeof(mreq));
      if (rv < 0) {
        return -errno;
      }
    } else {
      uint32_t interface_index = multicast_interface_;
      int rv = setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                          reinterpret_cast<const char*>(&interface_index),
                          sizeof(interface_index));
      if (rv < 0) {
        return -errno;
      }
    }
  }

  return 0;
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

int UDPSocket::SetReceiveBufferSize(int32_t size) {
  assert(sockfd_ != kInvalidSocket);

  int rv = ::setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF,
                        reinterpret_cast<const char*>(&size), sizeof(size));
  int last_error = errno;
  if (rv < 0) {
    LOG_SYSERR << "::setsockopt";
  }

  return rv == 0 ? 0 : -last_error;
}

int UDPSocket::SetSendBufferSize(int32_t size) {
  assert(sockfd_ != kInvalidSocket);

  int rv = ::setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF,
                        reinterpret_cast<const char*>(&size), sizeof(size));
  int last_error = errno;
  if (rv < 0) {
    LOG_SYSERR << "::setsockopt";
  }

  return rv == 0 ? 0 : -last_error;
}

int UDPSocket::Read(void* buf, size_t len) {
  return RecvFrom(buf, len, nullptr, 0);
}

int UDPSocket::RecvFrom(void* buf, size_t len, muduo::net::InetAddress* address,
                        int flags) {
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
  return SendToImpl(buf, len, nullptr, 0);
}

int UDPSocket::SendTo(const void* buf, size_t len,
                      const muduo::net::InetAddress& address, int flags) {
  return SendToImpl(buf, len, &address, flags);
}

int UDPSocket::SendToImpl(const void* buf, size_t len,
                          const muduo::net::InetAddress* address, int flags) {
  SockaddrStorage storage;
  struct sockaddr* addr = storage.addr;
  if (!address) {
    addr = nullptr;
    storage.addr_len = 0;
  } else {
    if (!SockaddrStorage::ToSockAddr(*address, &storage)) {
      return -EFAULT;
    }
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
