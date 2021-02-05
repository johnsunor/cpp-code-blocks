
#include <stdio.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "log_util.h"
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
    ASSERT_EXIT(port > 1023);

    EventLoop loop;
    UDPSocket socket;
    InetAddress address(ip, port);

    ERROR_EXIT(socket.Bind(address));

    Channel channel(&loop, socket.sockfd());
    channel.setReadCallback([&](Timestamp) {
      char buf[64 * 1024];
      InetAddress peer_address;
      int result = ERROR_EXIT(socket.RecvFrom(buf, sizeof(buf), &peer_address));

      ERROR_EXIT(socket.SendTo(buf, result, peer_address));
    });

    channel.enableReading();

    loop.loop();
  }
}
