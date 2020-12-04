
#include <stdio.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "udp_socket.h"

int main(int argc, char* argv[]) {
  using namespace muduo;
  using namespace muduo::net;

  if (argc != 3) {
    fprintf(stderr, "Usage: server <ip> <port>\n");
  } else {
    LOG_INFO << "pid = " << getpid() << ", tid = " << CurrentThread::tid();
    Logger::setLogLevel(Logger::WARN);

    const char* ip = argv[1];
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));

    EventLoop loop;
    UDPSocket socket;
    InetAddress address(ip, port);

    socket.AllowReuseAddress();
    socket.AllowReceiveError();
    assert(socket.Bind(address) == 0);

    Channel channel(&loop, socket.sockfd());
    channel.setReadCallback([&](Timestamp) {
      char buf[64 * 1024];
      InetAddress peer_address;
      int result = socket.RecvFrom(buf, sizeof(buf), &peer_address);
      assert(result > 0);

      result = socket.SendTo(buf, result, peer_address);
      assert(result > 0);
    });

    channel.setErrorCallback([] { assert(false); });
    channel.enableReading();

    loop.loop();
  }
}
