
#include <stdio.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "log_util.h"
#include "udp_socket.h"

int main(int argc, char* argv[]) {
  if (argc != 5) {
    fprintf(stderr, "Usage: client <ip> <port> <block_size> <timeout>\n");
  } else {
    LOG_INFO << "pid = " << getpid()
             << ", tid = " << muduo::CurrentThread::tid();
    muduo::Logger::setLogLevel(muduo::Logger::WARN);

    const char* ip = argv[1];
    const uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    const uint32_t block_size = static_cast<uint32_t>(atoi(argv[3]));
    const uint32_t timeout = static_cast<uint32_t>(atoi(argv[4]));

    ASSERT_EXIT(port > 1023);
    ASSERT_EXIT(block_size > 0);
    ASSERT_EXIT(timeout > 0);

    std::string message;
    for (uint32_t i = 0; i < block_size; ++i) {
      message.push_back(static_cast<char>(i % 128));
    }

    muduo::net::EventLoop loop;
    muduo::net::InetAddress address(ip, port);

    UDPSocket socket;
    ERROR_EXIT(socket.Connect(address));

    uint64_t total_bytes_read = 0;
    uint64_t total_bytes_write = 0;
    uint64_t total_messages_read = 0;
    muduo::net::Channel channel(&loop, socket.sockfd());
    channel.setReadCallback([&](auto) {
      char buf[64 * 1024];
      int result = ERROR_EXIT(socket.Read(buf, sizeof(buf)));

      ++total_messages_read;
      total_bytes_read += result;

      result = ERROR_EXIT(socket.Write(buf, result));

      total_bytes_write += result;
    });

    channel.enableReading();

    ERROR_EXIT(socket.Write(message.data(), message.size()));

    loop.runAfter(timeout, [&] {
      LOG_WARN << total_bytes_read << " total bytes read";
      LOG_WARN << total_messages_read << " total messages read";

      if (total_messages_read > 0) {
        LOG_WARN << static_cast<double>(total_bytes_read) /
                        static_cast<double>(total_messages_read)
                 << " average message size";
      }

      if (timeout > 0) {
        LOG_WARN << static_cast<double>(total_bytes_read) /
                        (timeout * 1024 * 1024)
                 << " MiB/s throughput";
      }

      loop.quit();
    });

    loop.loop();
  }
}
