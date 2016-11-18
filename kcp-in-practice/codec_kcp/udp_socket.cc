
#include "udp_socket.h"

UDPSocket::UDPSocket()
    : sockfd_(kInvalidSocket),
      addr_family_(0),
      socket_options_(SOCKET_OPTION_MULTICAST_LOOP) {}

int UDPSocket::Connect(const muduo::net::InetAddress& address) {
  assert(!is_connected());
  assert(!remote_address_.get());

  int addr_family = address.family();
  int rv = CreateSocket(addr_family);
  if (rv < 0) {
    return rv;
  }

  SockaddrStorage storage;
  if (!SockaddrStorage::ToSockAddr(address, &storage)) {
    Close();
    return -1;  // EFAULT;;
  }

  rv = HANDLE_EINTR(::connect(sockfd_, storage.addr, storage.addr_len));
  if (rv < 0) {
    Close();
    return rv;
  }

  remote_address_.reset(new muduo::net::InetAddress(address));
  return rv;
}

int UDPSocket::Bind(const muduo::net::InetAddress& address) {
  assert(!is_connected());

  int rv = CreateSocket(address.family());
  if (rv < 0) {
    return rv;
  }

  rv = SetSocketOptions();  // last error
  if (rv < 0) {
    Close();
    return rv;
  }

  rv = DoBind(address);
  if (rv < 0) {
    Close();
    return rv;
  }

  local_address_.reset();
  return rv;
}

int UDPSocket::DoBind(const muduo::net::InetAddress& address) {
  assert(sockfd_ != kInvalidSocket);

  SockaddrStorage storage;
  if (!SockaddrStorage::ToSockAddr(address, &storage)) {
    return -1;
  }

  int rv = ::bind(sockfd_, storage.addr, storage.addr_len);
  int last_error = errno;
  if (rv < 0) {
    LOG_SYSERR << "::bind";
  }

  return rv == 0 ? 0 : last_error;
}

int UDPSocket::CreateSocket(int addr_family) {
  assert(sockfd_ == kInvalidSocket);

  int sockfd = ::socket(addr_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                        IPPROTO_UDP);
  if (sockfd == kInvalidSocket) {
    LOG_SYSERR << "::socket";
    return errno;
  }

  addr_family_ = addr_family;
  sockfd_ = sockfd;

  return 0;
}

int UDPSocket::SetMulticastInterface(uint32_t interface_index) {
  assert(!is_connected());

  multicast_interface_ = interface_index;
  return 0;
}

int UDPSocket::SetMulticastTimeToLive(int time_to_live) {
  assert(!is_connected());

  if (time_to_live < 0 || time_to_live > 255) {
    return EINVAL;
  }

  multicast_time_to_live_ = time_to_live;
  return 0;
}

int UDPSocket::SetSocketOptions() {
  int true_value = 1;
  if (socket_options_ & SOCKET_OPTION_REUSE_ADDRESS) {
    int rv = setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &true_value,
                        sizeof(true_value));
    if (rv < 0) {
      return errno;
    }
  }

  if (socket_options_ & SOCKET_OPTION_REUSE_PORT) {
    int rv = setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &true_value,
                        sizeof(true_value));
    if (rv < 0) {
      return errno;
    }
  }

  if (socket_options_ & SOCKET_OPTION_BROADCAST) {
    int rv;
    rv = setsockopt(sockfd_, SOL_SOCKET, SO_BROADCAST, &true_value,
                    sizeof(true_value));
    if (rv < 0) {
      return errno;
    }
  }

  if (!(socket_options_ & SOCKET_OPTION_MULTICAST_LOOP)) {
    int rv;
    if (addr_family_ == AF_INET) {
      u_char loop = 0;
      rv = setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
                      sizeof(loop));
    } else {
      u_int loop = 0;
      rv = setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop,
                      sizeof(loop));
    }
    if (rv < 0) {
      return errno;
    }
  }

  if (multicast_time_to_live_ != IP_DEFAULT_MULTICAST_TTL) {
    int rv;
    if (addr_family_ == AF_INET) {
      u_char ttl = multicast_time_to_live_;
      rv = setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    } else {
      // Signed integer. -1 to use route default.
      int ttl = multicast_time_to_live_;
      rv = setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl,
                      sizeof(ttl));
    }
    if (rv < 0) {
      return errno;
    }
  }

  if (multicast_interface_ != 0) {
    switch (addr_family_) {
      case AF_INET: {
        ip_mreqn mreq;
        mreq.imr_ifindex = multicast_interface_;
        mreq.imr_address.s_addr = htonl(INADDR_ANY);
        int rv = setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_IF,
                            reinterpret_cast<const char*>(&mreq), sizeof(mreq));
        if (rv < 0) {
          return errno;
        }
        break;
      }
      case AF_INET6: {
        uint32_t interface_index = multicast_interface_;
        int rv = setsockopt(sockfd_, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                            reinterpret_cast<const char*>(&interface_index),
                            sizeof(interface_index));
        if (rv < 0) {
          return errno;
        }
        break;
      }

      default: {
        LOG_ERROR << "not reached";
        return -1;
      }
    }
  }

  return 0;
}

void UDPSocket::AllowAddressReuse() {
  assert(!is_connected());

  socket_options_ |= SOCKET_OPTION_REUSE_ADDRESS;
}

void UDPSocket::AllowBroadcast() {
  assert(!is_connected());

  socket_options_ |= SOCKET_OPTION_BROADCAST;
}

int UDPSocket::SetReceiveBufferSize(int32_t size) {
  assert(sockfd_ != kInvalidSocket);

  int rv = setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF,
                      reinterpret_cast<const char*>(&size), sizeof(size));
  int last_error = errno;
  if (rv < 0) {
    LOG_SYSERR << "::setsockopt";
  }

  return rv == 0 ? 0 : last_error;
}

int UDPSocket::SetSendBufferSize(int32_t size) {
  assert(sockfd_ != kInvalidSocket);

  int rv = setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF,
                      reinterpret_cast<const char*>(&size), sizeof(size));
  int last_error = errno;
  if (rv < 0) {
    LOG_SYSERR << "::setsockopt";
  }

  return rv == 0 ? 0 : last_error;
}

int UDPSocket::Read(void* buf, size_t len) { return RecvFrom(buf, len, NULL); }

int UDPSocket::RecvFrom(void* buf, size_t len,
                        muduo::net::InetAddress* address) {
  int bytes_transferred;
  int flags = 0;

  SockaddrStorage storage;

  bytes_transferred = HANDLE_EINTR(
      recvfrom(sockfd_, buf, len, flags, storage.addr, &storage.addr_len));

  int result;
  if (bytes_transferred >= 0) {
    if (address != NULL && !SockaddrStorage::ToInetAddr(storage, address)) {
      result = EINVAL;
    } else {
      result = bytes_transferred;
    }
  } else {
    int last_error = errno;
    if (last_error != EAGAIN && last_error != EWOULDBLOCK) {
      LOG_SYSERR << "::recvfrom";
    }
    result = last_error;
  }

  return result;
}

int UDPSocket::Write(const void* buf, size_t len) {
  return SendToOrWrite(buf, len, NULL);
}

int UDPSocket::SendTo(const void* buf, size_t len,
                      const muduo::net::InetAddress& address) {
  return SendToOrWrite(buf, len, &address);
}

int UDPSocket::SendToOrWrite(const void* buf, size_t len,
                             const muduo::net::InetAddress* address) {
  assert(sockfd_ != kInvalidSocket);
  assert(len > 0);

  int result = InternalSendTo(buf, len, address);
  if (result != EAGAIN && result != EWOULDBLOCK) {
    return result;
  }

  // assert(!send_to_address_.get());
  // if (address != NULL) {
  // send_to_address_.reset(new muduo::net::InetAddress(*address));
  //}

  return result;
}

int UDPSocket::InternalSendTo(const void* buf, size_t len,
                              const muduo::net::InetAddress* address) {
  SockaddrStorage storage;
  struct sockaddr* addr = storage.addr;
  if (!address) {
    addr = NULL;
    storage.addr_len = 0;
  } else {
    if (!SockaddrStorage::ToSockAddr(*address, &storage)) {
      return EFAULT;
    }
  }

  int flags = 0;
  int result =
      HANDLE_EINTR(::sendto(sockfd_, buf, len, flags, addr, storage.addr_len));
  if (result < 0) {
    int last_error = errno;
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      LOG_SYSERR << "::sendto";
    }

    return last_error;
  }

  return result;
}

void UDPSocket::Close() {
  if (!is_connected()) {
    return;
  }

  // Zero out any pending read/write callback state.
  if (IGNORE_EINTR(::close(sockfd_)) < 0) {
    LOG_SYSERR << "::close";
  }

  sockfd_ = kInvalidSocket;
  addr_family_ = 0;
}
