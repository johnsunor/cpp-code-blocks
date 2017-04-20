
#include <map>

#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/InetAddress.h>

#include <gflags/gflags.h>

#include "frontend_tunnel.h"

DEFINE_string(backend_ip, "0.0.0.0", "backend_ip");
DEFINE_int32(backend_port, 9527, "backend_port");
DEFINE_string(frontend_ip, "0.0.0.0", "frontend_ip");
DEFINE_int32(frontend_port, 9999, "frontend_port");

using namespace muduo;
using namespace muduo::net;

class KCPRelayFrontend {
 public:
  typedef std::map<muduo::string, FrontendTunnelPtr> TunnelMap;

  KCPRelayFrontend(EventLoop* loop, const InetAddress& server_addr)
      : server_(loop, server_addr, "KCPRelayFrontend") {
    server_.setConnectionCallback(
        boost::bind(&KCPRelayFrontend::OnClientConnection, this, _1));
    server_.setMessageCallback(
        boost::bind(&KCPRelayFrontend::onServerMessage, this, _1, _2, _3));
  }

  void Start() { server_.start(); }

  void OnClientConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
      conn->setTcpNoDelay(true);
      conn->stopRead();
      muduo::net::InetAddress backend_addr(FLAGS_backend_ip,
                                           FLAGS_backend_port);
      FrontendTunnelPtr tunnel(
          new FrontendTunnel(conn->getLoop(), backend_addr, conn));
      tunnel->setup();
      tunnel->connect();
      all_tunnels[conn->name()] = tunnel;
    } else {
      conn->setContext(boost::any());
      conn->getLoop()->queueInLoop(
          boost::bind(&KCPRelayFrontend::DestoryTunnel, this, conn->name()));
    }
  }

  void DestoryTunnel(const string& name) {
    TunnelMap::iterator tunnel_it = all_tunnels.find(name);
    if (tunnel_it != all_tunnels.end()) {
      tunnel_it->second->Disconnect();
      all_tunnels.erase(tunnel_it);
    }
  }

  void onServerMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buf, muduo::Timestamp) {
    if (!conn->getContext().empty()) {
      const KCPSessionPtr& kcp_session =
          boost::any_cast<const KCPSessionPtr&>(conn->getContext());
      kcp_session->Send(buf);
    } else {
      conn->shutdown();
      LOG_ERROR << "no kcp_session readableBytes: " << buf->readableBytes();
    }
  }

 private:
  muduo::net::TcpServer server_;

  TunnelMap all_tunnels;
};

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  LOG_INFO << "FLAGS_frontend_ip: " << FLAGS_frontend_ip
           << ", FLAGS_frontend_port: " << FLAGS_frontend_port
           << ", FLAGS_backend_ip: " << FLAGS_backend_ip
           << ", FLAGS_backend_port: " << FLAGS_backend_port;

  LOG_INFO << "pid = " << getpid() << ", tid = " << CurrentThread::tid();

  muduo::g_logLevel = muduo::Logger::WARN;

  EventLoop loop;

  InetAddress listen_addr(FLAGS_frontend_ip, FLAGS_frontend_port);
  KCPRelayFrontend frontend(&loop, listen_addr);

  frontend.Start();
  loop.loop();

  return 0;
}
