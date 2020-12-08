
#include <stdio.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "kcp_callbacks.h"
#include "kcp_server.h"
#include "kcp_session.h"

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
    InetAddress address(ip, port);

    EventLoop loop;

    KCPServer server(&loop);

    server.set_connection_callback(
        [](const KCPSessionPtr& session, bool connected) {
          LOG_WARN << "session: " << session->session_id()
                   << (connected ? " up" : " down");
        });
    server.set_message_callback(
        [](const KCPSessionPtr& session, Buffer* buf) { session->Write(buf); });

    server.ListenOrDie(address);

    loop.loop();
  }
}
