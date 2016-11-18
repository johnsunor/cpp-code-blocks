
#include <muduo/net/EventLoop.h>

#include "udp_client.h"

using namespace muduo;
using namespace muduo::net;

EventLoop loop;
UDPClient client(&loop);

void OnMessage(Buffer* buf, Timestamp time, const InetAddress& address) {
  LOG_INFO << buf->toStringPiece() << ", time:" << time.toString()
           << ", from " << address.toIpPort();
  client.Write(buf->peek(), buf->readableBytes());
}

int main(int argc, char* argv[]) {
  InetAddress address("127.0.0.1", ::atoi(argv[1]));

  LOG_INFO << "server address: " << address.toIpPort();

  client.Connect(address);
  client.set_message_callback(OnMessage);
  client.Start();

  std::string msg = "Hello World";
  client.Write(msg.data(), msg.size());

  loop.loop();

  return 0;
}
