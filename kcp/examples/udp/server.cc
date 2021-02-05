
#include <stdio.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "log_util.h"
#include "udp_socket.h"

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
  } else {
    LOG_INFO << "pid = " << getpid()
             << ", tid = " << muduo::CurrentThread::tid();
    muduo::Logger::setLogLevel(muduo::Logger::WARN);

    const char* ip = argv[1];
    const uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    ASSERT_EXIT(port > 1023);

    muduo::net::EventLoop loop;
    muduo::net::InetAddress address(ip, port);

    UDPSocket socket;
    ERROR_EXIT(socket.Bind(address));

    muduo::net::Channel channel(&loop, socket.sockfd());
    channel.setReadCallback([&](auto) {
      char buf[64 * 1024];
      muduo::net::InetAddress peer_address;
      int result = ERROR_EXIT(socket.RecvFrom(buf, sizeof(buf), &peer_address));

      ERROR_EXIT(socket.SendTo(buf, result, peer_address));
    });

    channel.enableReading();

    loop.loop();
  }
}
