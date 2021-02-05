
#include <stdio.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "kcp_callbacks.h"
#include "kcp_client.h"
#include "kcp_session.h"
#include "log_util.h"

int main(int argc, char* argv[]) {
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <ip> <port> <block_size> <timeout_sec>\n",
            argv[0]);
  } else {
    LOG_INFO << "pid = " << getpid()
             << ", tid = " << muduo::CurrentThread::tid();
    muduo::Logger::setLogLevel(muduo::Logger::WARN);

    const char* ip = argv[1];
    const uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    const int block_size = atoi(argv[3]);
    const double timeout_sec = atof(argv[4]);

    ASSERT_EXIT(port > 1023);
    ASSERT_EXIT(block_size > 0 && block_size < 1024 * 1024);
    ASSERT_EXIT(timeout_sec > 0 && timeout_sec < 10 * 60);

    uint64_t total_bytes_read = 0;
    uint64_t total_bytes_write = 0;
    uint64_t total_messages_read = 0;

    std::string message;
    message.reserve(block_size);
    for (int i = 0; i < block_size; ++i) {
      message.push_back(static_cast<char>(i % 128));
    }

    muduo::net::EventLoop loop;
    muduo::net::InetAddress address(ip, port);

    KCPClient client(&loop);
    client.set_connection_callback(
        [&](const KCPSessionPtr& session, bool connected) {
          if (connected) {
            session->Write(message.data(), message.size());
          } else {
            LOG_WARN << "session disconnected";

            LOG_WARN << total_bytes_read << " total bytes read";
            LOG_WARN << total_messages_read << " total messages read";

            if (total_messages_read > 0) {
              LOG_WARN << static_cast<double>(total_bytes_read) /
                              static_cast<double>(total_messages_read)
                       << " average message size";
            }

            if (timeout_sec > 0) {
              LOG_WARN << static_cast<double>(total_bytes_read) /
                              (timeout_sec * 1024 * 1024)
                       << " MiB/s throughput";
            }

            session->loop()->quit();
          }
        });

    client.set_message_callback(
        [&](const KCPSessionPtr& session, muduo::net::Buffer* buf) {
          ++total_messages_read;
          total_bytes_read += buf->readableBytes();
          total_bytes_write += buf->readableBytes();
          session->Write(buf);
        });
    client.ConnectOrDie(address);

    loop.runAfter(timeout_sec, [&] {
      if (client.state() == KCPClient::CONNECTED) {
        client.Disconnect();
      } else {
        LOG_ERROR << "client build connection failed";
        loop.quit();
      }
    });

    loop.loop();
  }
}
