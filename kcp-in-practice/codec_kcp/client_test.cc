#include <stdio.h>

#include <muduo/net/EventLoop.h>

#include "udp_client.h"

using namespace muduo;
using namespace muduo::net;

class TestClient {
 public:
  TestClient(EventLoop* loop, const InetAddress& addr) : udp_client_(loop) {
    udp_client_.set_message_callback(
        boost::bind(&TestClient::OnMessage, this, _1, _2));
    udp_client_.Connect(addr);
  }

  void Start() { udp_client_.Start(); }

  void OnMessage(Buffer* buf, Timestamp receive_time) {
    muduo::net::InetAddress peer_addr;
    udp_client_.GetPeerAddress(&peer_addr);

    LOG_INFO << "recv msg: " << buf->toStringPiece()
             << ", at: " << receive_time.toString() << ", from "
             << peer_addr.toIpPort();
    int result =
        udp_client_.WriteOrQueuePcket(buf->peek(), buf->readableBytes());

    assert(implicit_cast<size_t>(result) == buf->readableBytes());

    buf->retrieveAll();
  }

  void Write(const std::string& msg) {
    udp_client_.Write(msg.data(), msg.size());
  }

 private:
  UDPClient udp_client_;
};

int main(int argc, char* argv[]) {
  if (argc != 3) {
    printf("Usage: %s ip port\n", argv[0]);
    exit(0);
  }

  EventLoop loop;
  InetAddress address(argv[1], ::atoi(argv[2]));
  TestClient client(&loop, address);

  client.Start();

  LOG_INFO << "connecting to " << address.toIpPort();

  std::string msg = "Hello World";
  client.Write(msg);

  loop.loop();

  return 0;
}
