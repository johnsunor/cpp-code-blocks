
#include <map>
#include <queue>

#include <boost/bind.hpp>
#include <boost/unordered_map.hpp>

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>

#include "ikcp.h"
#include "string_utils.h"

using namespace muduo;
using namespace muduo::net;

const double kClientInterval = 1;

const int kMaxSessionId = 1000;
const int kFrameLen = sizeof(int64_t) * 2;

boost::unordered_map<std::string, IKCPCB*> kcpcbs;
boost::unordered_map<std::string, TimerId> timer_ids;
std::queue<int> avail_session_ids;

int CreateNonblockingUDPOrDie() {
  int sockfd =
      ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
  if (sockfd < 0) {
    LOG_SYSFATAL << "::socket";
  }
  return sockfd;
}

struct SockAddr {
  SockAddr(int fd, socklen_t len, const sockaddr& addr) {
    sockfd = fd;
    peer_addr_len = len;
    ::memcpy(&peer_addr, &addr, sizeof peer_addr);
  }

  int sockfd;
  socklen_t peer_addr_len;
  struct sockaddr peer_addr;
};

void InitAvailSessionIds() {
  for (int i = 1; i <= kMaxSessionId; ++i) {
    avail_session_ids.push(i);
  }
}

int GetNextSessionId() {
  if (avail_session_ids.empty()) {
    return 0;
  }

  int id = avail_session_ids.front();
  avail_session_ids.pop();

  return id;
}

void ReleaseSessionId(int session_id) {
  avail_session_ids.push(session_id);
}

int ServerOutput(const char* buf, int len, IKCPCB* kcp, void* user) {
  (void)kcp;

  SockAddr* addr = reinterpret_cast<SockAddr*>(user);

  int ns = ::sendto(addr->sockfd, buf, implicit_cast<size_t>(len), 0,
                    &addr->peer_addr, addr->peer_addr_len);
  if (ns < 0) {
    LOG_SYSERR << "::sendto";
  }

  return 0;
}

bool SetupFastMode(IKCPCB* kcp) {
  assert(kcp != NULL);

  int rv = ikcp_nodelay(kcp, 1, 10, 2, 1);
  if (rv < 0) {
    return false;
  }

  rv = ikcp_wndsize(kcp, 128, 128);
  if (rv < 0) {
    return false;
  }

  return true;
}

void TryUpdateKCPCB(EventLoop* loop, const std::string& addr_str) {
  boost::unordered_map<std::string, IKCPCB*>::iterator it =
      kcpcbs.find(addr_str);
  if (it == kcpcbs.end()) {
    return;
  }

  boost::unordered_map<std::string, TimerId>::iterator id_it =
      timer_ids.find(addr_str);
  if (id_it != timer_ids.end()) {
    loop->cancel(id_it->second);
  }

  IKCPCB* kcpcb = it->second;
  uint64_t now_micro_s = Timestamp::now().microSecondsSinceEpoch();
  uint32_t now = now_micro_s / 1000 & (~0);
  uint32_t ts = ikcp_check(kcpcb, now);
  if (ts <= now) {
    LOG_INFO << "update kcp session now, session_id: " << kcpcb->conv;

    ikcp_update(kcpcb, now);
    if (static_cast<int>(kcpcb->state) == -1) {
      kcpcbs.erase(addr_str);
      LOG_INFO << "dead link with addr_str: " << addr_str;
    }
  } else {
    assert(false);
    LOG_INFO << "add timer for kcp session, session_id: " << kcpcb->conv;

    Timestamp time(now_micro_s + (ts - now) * 1000);
    TimerId timer_id =
        loop->runAt(time, boost::bind(TryUpdateKCPCB, loop, addr_str));
    timer_ids[addr_str] = timer_id;
  }
}

void ServerReadCallback(EventLoop* loop, int sockfd,
                        muduo::Timestamp receiveTime) {
  static char buf[64 * 1024];

  struct sockaddr peer_addr;
  socklen_t addr_len = sizeof peer_addr;
  ssize_t nr = ::recvfrom(sockfd, buf, sizeof buf, 0, &peer_addr, &addr_len);
  if (nr < 0) {
    LOG_SYSERR << "::recvfrom";
    return;
  }

  std::string addr_str;
  char* ptr = reinterpret_cast<char*>(utils::WriteInto(&addr_str, 32));
  sockets::toIpPort(ptr, addr_str.size(), &peer_addr);
  LOG_INFO << "c -----> s bytes: " << nr << " from " << addr_str;

  boost::unordered_map<std::string, IKCPCB*>::iterator it =
      kcpcbs.find(addr_str);
  if (it == kcpcbs.end()) {
    if (nr != 1) {
      LOG_ERROR << "need one byte to init kcp session";
      return;
    }

    if (buf[0] != '\n') {
      LOG_ERROR << "need `\\n` to init kcp session";
      return;
    }

    int session_id = GetNextSessionId();
    if (session_id == 0) {
      LOG_INFO << "no session id available";
      return;
    }

    int nw = ::sendto(sockfd, &session_id, sizeof(session_id), 0, &peer_addr,
                      addr_len);
    if (implicit_cast<size_t>(nw) != sizeof(session_id)) {
      ReleaseSessionId(session_id);
      LOG_ERROR << "need `\\n` to init kcp session";
      return;
    }

    SockAddr* addr = new SockAddr(sockfd, addr_len, peer_addr);

    IKCPCB* kcpcb = ikcp_create(session_id, addr);
    if (!SetupFastMode(kcpcb)) {
      ikcp_release(kcpcb);
      ReleaseSessionId(session_id);
      LOG_ERROR << "SetupFastMode failed with kcp, session_id: " << session_id;
      return;
    }

    ikcp_setoutput(kcpcb, ServerOutput);
    kcpcbs[addr_str] = kcpcb;
  }

  IKCPCB* kcpcb = kcpcbs[addr_str];

  int rv = ikcp_input(kcpcb, implicit_cast<const char*>(buf), nr);
  if (rv < 0) {
    if (rv == -2) {
      LOG_ERROR << "ikcp_input rv = -2";
    } else {
      LOG_ERROR << "kcp_input maybe data has been truncated rv: " << rv;
      return;
    }
  }

  while (true) {
    int pkg_size = ikcp_peeksize(kcpcb);
    if (pkg_size < 0) {
      break;
    }
    LOG_INFO << "c -----> s peek udp pkg bytes: " << pkg_size << " from "
             << addr_str;

    if (implicit_cast<size_t>(pkg_size) != kFrameLen) {
      LOG_FATAL << "udp pkg size error with unexpected pkg size: " << pkg_size;
    }

    std::string msg;
    char* ptr = reinterpret_cast<char*>(utils::WriteInto(&msg, pkg_size + 1));
    int rn = ikcp_recv(kcpcb, ptr, pkg_size);
    if (rn < 0) {
      LOG_FATAL << "::ikcp_recv impossible";
    }

    int64_t* message = reinterpret_cast<int64_t*>(ptr);
    message[1] = receiveTime.microSecondsSinceEpoch() / 1000;

    int nw = ikcp_send(kcpcb, reinterpret_cast<const char*>(message),
                       static_cast<int>(kFrameLen));
    if (nw < 0) {
      LOG_ERROR << "ikcp_send, nw: " << nw;
    }
  }

  TryUpdateKCPCB(loop, addr_str);
}

void RunServer(uint16_t port) {
  Socket sock(CreateNonblockingUDPOrDie());
  sock.bindAddress(InetAddress(port));

  EventLoop loop;
  Channel channel(&loop, sock.fd());
  channel.setReadCallback(
      boost::bind(&ServerReadCallback, &loop, sock.fd(), _1));
  channel.enableReading();
  loop.loop();
}

#include <stdio.h>

int main(int argc, char* argv[]) {
  InitAvailSessionIds();

  if (argc > 2) {
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    if (strcmp(argv[1], "-s") == 0) {
      RunServer(port);
    }
  } else {
    printf("Usage:\n%s -s port\n%s ip port\n", argv[0], argv[0]);
  }
}
