
#include <stdio.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "kcp_callbacks.h"
#include "kcp_server.h"
#include "kcp_session.h"
#include "log_util.h"

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

    muduo::net::InetAddress address(ip, port);

    muduo::net::EventLoop loop;

    KCPServer server(&loop);

    server.set_connection_callback(
        [](const KCPSessionPtr& session, bool connected) {
          LOG_WARN << "session: " << session->session_id()
                   << (connected ? " up" : " down");
        });
    server.set_message_callback(
        [](const KCPSessionPtr& session, muduo::net::Buffer* buf) {
          session->Write(buf);
        });

    server.ListenOrDie(address);

    loop.loop();
  }
}
