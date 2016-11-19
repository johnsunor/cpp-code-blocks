
#include <stdio.h>

#include <muduo/net/EventLoop.h>

#include "udp_server.h"

using namespace muduo;
using namespace muduo::net;

class TestServer {
 public:
  TestServer(EventLoop* loop, const InetAddress& addr) : udp_server_(loop) {
    udp_server_.set_message_callback(
        boost::bind(&TestServer::OnMessage, this, _1, _2, _3));
    udp_server_.ListenOrDie(addr);
  }

  void Start() { udp_server_.Start(); }

  void OnMessage(Buffer* buf, Timestamp receive_time,
                 const InetAddress& peer_addr) {
    LOG_INFO << "recv msg: " << buf->toStringPiece()
             << ", at: " << receive_time.toString() << ", from "
             << peer_addr.toIpPort();
    int result = udp_server_.SendOrQueuePcket(buf->peek(), buf->readableBytes(),
                                              peer_addr);

    assert(implicit_cast<size_t>(result) == buf->readableBytes());

    buf->retrieveAll();
  }

 private:
  UDPServer udp_server_;
};

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Usage: %s port\n", argv[0]);
    exit(0);
  }

  EventLoop loop;
  InetAddress address(::atoi(argv[1]));
  TestServer server(&loop, address);

  LOG_INFO << "server listen on: " << address.toIpPort();

  server.Start();

  loop.loop();

  return 0;
}
