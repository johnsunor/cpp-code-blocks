
#include <memory>
#include <string>

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "udp_socket.h"

#include "log_util.h"

void TestAddress() {
  // special
  ASSERT_EXIT(UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("224.0.0.0", 5050, false)));
  ASSERT_EXIT(UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("224.0.0.255", 5050, false)));

  // normal
  ASSERT_EXIT(UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("224.0.1.1", 5050, false)));
  ASSERT_EXIT(UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("239.0.0.1", 5050, false)));
  ASSERT_EXIT(UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("239.255.255.255", 5050, false)));
  // expected false
  ASSERT_EXIT(!UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("223.255.255.255", 5050, false)));
  ASSERT_EXIT(!UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("240.0.0.0", 5050, false)));

  // special
  ASSERT_EXIT(UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("ff01::1", 5050, true)));
  ASSERT_EXIT(UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("ff02::1", 5050, true)));
  // normal
  ASSERT_EXIT(UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("ff80::fe3:fe9a:4ca3", 5050, true)));
  // expected false
  ASSERT_EXIT(!UDPSocket::IsAddressMulticast(
      muduo::net::InetAddress("fe80::f816:3eff:feee:70b", 5050, true)));

  LOG_INFO << "TestAddress passed";
}

void TestMessage() {
  const char* in_ifname = "eth0";
  const char* out_ifname = "eth0";
  const uint8_t out_ttl = 0;   // default 1
  const bool out_loop = true;  // default true
  const char* mcast_addr = "239.0.0.1";
  const uint16_t mcast_port = 5050;
  const uint32_t end_id = 20;

  muduo::net::InetAddress group_address(mcast_addr, mcast_port);

  auto receiver_socket = std::make_unique<UDPSocket>();

  receiver_socket->AllowReuseAddress();

  ERROR_EXIT(receiver_socket->Bind(group_address));

  ERROR_EXIT(receiver_socket->JoinMulticastGroup(group_address, in_ifname));

  muduo::net::EventLoop loop;
  auto channel =
      std::make_unique<muduo::net::Channel>(&loop, receiver_socket->sockfd());

  uint32_t num_messages_received = 0;
  channel->setReadCallback([&](auto) {
    char buf[64];
    int len = ERROR_EXIT(receiver_socket->Read(buf, sizeof(buf)));
    LOG_INFO << "received message: " << std::string(buf, len);
    ++num_messages_received;
  });
  channel->enableReading();

  uint32_t id = 0;
  auto sender_socket = std::make_unique<UDPSocket>();
  ERROR_EXIT(sender_socket->Connect(group_address));
  ERROR_EXIT(sender_socket->SetMulticastLoop(out_loop));
  ERROR_EXIT(sender_socket->SetMulticastTTL(out_ttl));
  ERROR_EXIT(sender_socket->SetMulticastIF(out_ifname));

  loop.runEvery(1, [&] {
    std::string message = "message " + std::to_string(++id);
    ERROR_EXIT(sender_socket->Write(message.data(), message.length()));
    if (id == end_id) {
      ERROR_EXIT(
          receiver_socket->LeaveMulticastGroup(group_address, in_ifname));
    }
  });

  loop.runAfter(end_id + 5, [&] {
    ASSERT_EXIT(num_messages_received == end_id);
    loop.quit();
  });

  loop.loop();

  LOG_INFO << "TestMessage passed";
}

int main() {
  TestAddress();
  TestMessage();
}
