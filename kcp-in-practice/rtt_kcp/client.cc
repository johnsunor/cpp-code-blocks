#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>

#include <boost/bind.hpp>

#include <stdio.h>

#include "ikcp.h"
#include "string_utils.h"

using namespace muduo;
using namespace muduo::net;

const size_t frameLen = 2 * sizeof(int64_t);

#include <map>
std::map<int, ikcpcb*> kcp_map;

ikcpcb* client_kcp = NULL;

double clientInterval = 0.001;
double serverInterval = 0.01;

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

int ClientOutput(const char* buf, int len, struct IKCPCB* kcp, void* user) {
  assert(kcp == client_kcp);

  int sockfd = *reinterpret_cast<int*>(&user);
  ssize_t nw = sockets::write(sockfd, buf, len);
  if (nw < 0) {
    LOG_SYSERR << "::write";
  }

  LOG_INFO << "c -----> s bytes: " << nw;

  return 0;
}

void ClientReadCallback(int sockfd, muduo::Timestamp receiveTime) {
  static char buf[64 * 1024];
  ssize_t nr = ::read(sockfd, buf, sizeof buf);
  if (nr < 0) {
    LOG_SYSERR << "::read";
    return;
  }
  LOG_INFO << "s -----> c bytes: " << nr;

  if (nr == sizeof(int)) {
    int session_id = *reinterpret_cast<int*>(buf);
    client_kcp = ikcp_create(session_id, reinterpret_cast<void*>(sockfd));
    assert(SetupFastMode(client_kcp));
    ikcp_setoutput(client_kcp, ClientOutput);
    assert(client_kcp != NULL);
    LOG_INFO << "s -----> c init kcp session ok, session_id: " << session_id;
    return;
  }

  int rv = ikcp_input(client_kcp, implicit_cast<const char*>(buf), nr);
  if (rv < 0) {
    if (rv == -2) {
      LOG_ERROR << "::ikcp_input rv = -2";
    } else {
      LOG_SYSERR << "::ikcp_input";
      return;
    }
  }

  while (true) {
    int pkg_size = ikcp_peeksize(client_kcp);
    if (pkg_size < 0) {
      break;
    }

    LOG_INFO << "s -----> c payload bytes: " << pkg_size;

    std::string msg;
    char* ptr = reinterpret_cast<char*>(utils::WriteInto(&msg, pkg_size + 1));
    int real_size = ikcp_recv(client_kcp, ptr, pkg_size);
    if (real_size < 0) {
      LOG_FATAL << "ikcp_recv impossible";
    }

    if (implicit_cast<size_t>(real_size) != frameLen) {
      LOG_ERROR << "udp pkg size error with unexpected pkg size: " << pkg_size;
      continue;
    }

    int64_t* message = reinterpret_cast<int64_t*>(ptr);
    int64_t send = message[0];
    int64_t their = message[1];
    int64_t back = receiveTime.microSecondsSinceEpoch() / 1000;
    int64_t mine = (back + send) / 2;
    LOG_INFO << "round trip " << back - send << " clock error " << their - mine;
  }
}

void SendMyTime(int sockfd) {
  int64_t message[2] = {0, 0};
  message[0] = Timestamp::now().microSecondsSinceEpoch() / 1000;

  ssize_t nw = ikcp_send(client_kcp, reinterpret_cast<const char*>(message),
                         sizeof message);
  if (nw < 0) {
    LOG_SYSERR << "::ikcp_send";
    return;
  }

  int now = static_cast<int>(message[0]) & (~0);
  ikcp_update(client_kcp, now);

  if (static_cast<int>(client_kcp->state) == -1) {
    LOG_ERROR << "client dead link";
    abort();
  }
}

int createNonblockingUDP() {
  int sockfd =
      ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
  if (sockfd < 0) {
    LOG_SYSFATAL << "::socket";
  }
  return sockfd;
}

void runClient(const char* ip, uint16_t port) {
  Socket sock(createNonblockingUDP());
  InetAddress serverAddr(ip, port);

  int ret = sockets::connect(sock.fd(), serverAddr.getSockAddr());
  if (ret < 0) {
    LOG_SYSFATAL << "::connect";
  }

  char init_ch = '\n';
  ssize_t nw = ::write(sock.fd(), &init_ch, sizeof init_ch);
  assert(nw == 1);

  EventLoop loop;
  Channel channel(&loop, sock.fd());
  channel.setReadCallback(boost::bind(&ClientReadCallback, sock.fd(), _1));
  channel.enableReading();
  loop.runEvery(clientInterval, boost::bind(SendMyTime, sock.fd()));
  loop.loop();
}

int main(int argc, char* argv[]) {
  if (argc > 2) {
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    runClient(argv[1], port);
  } else {
    printf("Usage:\n%s -s port\n%s ip port\n", argv[0], argv[0]);
  }
}
