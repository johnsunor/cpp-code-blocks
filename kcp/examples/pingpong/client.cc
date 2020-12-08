#include <stdio.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "kcp_callbacks.h"
#include "kcp_client.h"
#include "kcp_session.h"

int main(int argc, char* argv[]) {
  using namespace muduo;
  using namespace muduo::net;

  if (argc != 5) {
    fprintf(stderr, "Usage: client <ip> <port> <block_size> <timeout>\n");
  } else {
    LOG_INFO << "pid = " << getpid() << ", tid = " << CurrentThread::tid();
    Logger::setLogLevel(Logger::WARN);

    const char* ip = argv[1];
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    uint32_t block_size = static_cast<uint32_t>(atoi(argv[3]));
    uint32_t timeout = static_cast<uint32_t>(atoi(argv[4]));

    assert(block_size > 0);
    assert(timeout > 0);

    uint64_t total_bytes_read = 0;
    uint64_t total_bytes_write = 0;
    uint64_t total_messages_read = 0;

    string message;
    for (uint32_t i = 0; i < block_size; ++i) {
      message.push_back(static_cast<char>(i % 128));
    }

    EventLoop loop;
    InetAddress server_address(ip, port);

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

            if (timeout > 0) {
              LOG_WARN << static_cast<double>(total_bytes_read) /
                              (timeout * 1024 * 1024)
                       << " MiB/s throughput";
            }

            session->loop()->quit();
          }
        });

    client.set_message_callback([&](const KCPSessionPtr& session, Buffer* buf) {
      ++total_messages_read;
      total_bytes_read += buf->readableBytes();
      total_bytes_write += buf->readableBytes();
      session->Write(buf);
    });
    client.ConnectOrDie(server_address);

    loop.runAfter(timeout, [&] {
      if (client.state() == KCPClient::CONNECTED) {
        client.Disconnect();
      } else {
        LOG_WARN << "client build connection failed";
        loop.quit();
      }
    });

    loop.loop();
  }
}
