
#include <gflags/gflags.h>

#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>

#include "udp/udp_server.h"
#include "kcp/kcp_session.h"

#include "backend_tunnel.h"

using namespace muduo;
using namespace muduo::net;

DEFINE_string(target_ip, "127.0.0.1", "target_ip");
DEFINE_int32(target_port, 8080, "target_port");
DEFINE_string(tcp_server_ip, "127.0.0.1", "tcp_server_ip");
DEFINE_int32(tcp_server_port, 9527, "tcp_server_port");
DEFINE_string(udp_server_ip, "127.0.0.1", "udp_server_ip");
DEFINE_int32(udp_server_port, 9528, "udp_server_port");
// DEFINE_int32(thread_num, 1, "server thread num");

class KCPRelayBackend {
 public:
  struct SessionInfo {
    uint32_t session_id;
    uint32_t key;
    TcpConnectionPtr conn;
  };

  typedef std::map<uint32_t, SessionInfo> SessionMap;
  typedef std::map<muduo::string, uint32_t> ConnNameMap;
  typedef std::map<muduo::string, BackendTunnelPtr> TunnelMap;

  KCPRelayBackend(EventLoop* loop, const InetAddress& tcp_server_addr,
                  const InetAddress& udp_server_addr)
      : tcp_server_(loop, tcp_server_addr, "KCPRelayBackend"),
        udp_server_(loop, udp_server_addr) {
    tcp_server_.setConnectionCallback(
        boost::bind(&KCPRelayBackend::OnTcpServerConnection, this, _1));

    udp_server_.set_message_callback(
        boost::bind(&KCPRelayBackend::OnUDPServerMessage, this, _1, _2, _3));
  }

  void Start() {
    tcp_server_.start();
    udp_server_.Start();
  }

  void SendMetaData(uint8_t kind, uint32_t session_id, uint32_t key,
                    const muduo::net::InetAddress& addr) {
    const MetaData data = {kind,
                           muduo::net::sockets::hostToNetwork32(session_id),
                           muduo::net::sockets::hostToNetwork32(key)};
    udp_server_.SendOrQueuePacket(reinterpret_cast<const char*>(&data),
                                  sizeof(data), addr);
  }

  void OnTargetConnection(const muduo::net::TcpConnectionPtr& conn,
                          uint32_t session_id, uint32_t key,
                          const muduo::net::InetAddress& addr) {
    if (conn->connected()) {
      SendMetaData(MetaData::kAck, session_id, key, addr);
    }
  }

  void OnUDPServerMessage(muduo::net::Buffer* buf, muduo::Timestamp,
                          const muduo::net::InetAddress& peer_address) {
    assert(buf->readableBytes() >= sizeof(MetaData));

    uint8_t kind = implicit_cast<uint8_t>(buf->readInt8());
    uint32_t session_id = implicit_cast<uint32_t>(buf->readInt32());
    uint32_t key = implicit_cast<uint32_t>(buf->readInt32());

    SessionMap::iterator session_it = all_sessions_.find(session_id);
    if (session_it == all_sessions_.end()) {
      LOG_ERROR << "session not exists, session_id: " << session_id;
      return;
    } else if (session_it->second.key != key) {
      LOG_ERROR << "session key invalid, session_id: " << session_id
                << ", invalid key: " << key
                << ", valid key: " << session_it->second.key;
      return;
    }

    const TcpConnectionPtr& conn = session_it->second.conn;
    if (kind == MetaData::kSyn) {
      if (!conn->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(conn->getContext());
        if (kcp_session->session_id() == session_id &&
            kcp_session->key() == key) {
          SendMetaData(MetaData::kAck, session_id, key, peer_address);
        }
      } else {
        KCPSessionPtr kcp_session(new KCPSession(conn->getLoop()));
        assert(kcp_session->Init(session_id, key, kFastModeKCPParams));

        kcp_session->set_send_no_delay(true);
        kcp_session->set_peer_address(peer_address);
        kcp_session->set_message_callback(
            boost::bind(&KCPRelayBackend::OnKCPMessage, this, _1, _2));
        kcp_session->set_output_callback(
            boost::bind(&KCPRelayBackend::SendUDPMessage, this, _1, _2));
        kcp_session->set_close_callback(
            boost::bind(&KCPRelayBackend::OnKCPSessionClose, this, _1));

        conn->setContext(kcp_session);

        muduo::net::InetAddress target_server_addr(FLAGS_target_ip,
                                                   FLAGS_target_port);
        BackendTunnelPtr tunnel(
            new BackendTunnel(conn->getLoop(), target_server_addr, conn));
        tunnel->set_target_connection_callback(
            boost::bind(&KCPRelayBackend::OnTargetConnection, this, _1,
                        session_id, key, peer_address));
        tunnel->setup();
        tunnel->connect();
        all_tunnels.insert(std::make_pair(conn->name(), tunnel));
      }
    } else if (kind == MetaData::kPsh) {
      if (!conn->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(conn->getContext());
        if (kcp_session->session_id() == session_id &&
            kcp_session->key() == key) {
          kcp_session->Feed(buf->peek(), buf->readableBytes());
        }
      }
    } else {
      LOG_ERROR << "recved unexpected kind: " << kind << " from "
                << peer_address.toIpPort();
    }
  }

  void OnKCPMessage(const KCPSessionPtr& kcp_session, muduo::net::Buffer* buf) {
    LOG_INFO << "send target bytes: " << buf->readableBytes();
    if (!kcp_session->context().empty()) {
      const muduo::net::TcpConnectionPtr& target_conn =
          boost::any_cast<const muduo::net::TcpConnectionPtr&>(
              kcp_session->context());
      target_conn->send(buf);
    } else {
      LOG_WARN << "target conn not exists ";
    }
  }

  void SendSessionInitInfo(const muduo::net::TcpConnectionPtr& conn,
                           uint32_t session_id, uint32_t key) {
    muduo::net::InetAddress addr;
    int result = udp_server_.GetLocalAddress(&addr);
    if (result == 0) {
      muduo::net::Buffer buf;
      buf.appendInt16(addr.toPort());
      buf.appendInt32(session_id);
      buf.appendInt32(key);
      conn->send(&buf);

      LOG_INFO << "SendSessionInitInfo - session_id: " << session_id
               << ", key: " << key << ", port:" << addr.toIpPort();
    }
  }

  void OnTcpServerConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
      conn->setTcpNoDelay(true);

      uint32_t session_id =
          KCPSessionIdInitSingleton::GetInstance().GetNextSessionId();
      if (session_id != kInvalidSessionId) {
        uint32_t key = rand();
        SendSessionInitInfo(conn, session_id, key);
        SessionInfo session = {session_id, key, conn};
        all_sessions_.insert(std::make_pair(session_id, session));
        all_conn_names_.insert(std::make_pair(conn->name(), session_id));
      } else {
        conn->shutdown();
      }
    } else {
      if (!conn->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(conn->getContext());
        kcp_session->set_context(boost::any());

        KCPSessionIdInitSingleton::GetInstance().ReleaseSessionId(
            kcp_session->session_id());
        conn->setContext(boost::any());
      }

      ConnNameMap::iterator conn_it = all_conn_names_.find(conn->name());
      if (conn_it != all_conn_names_.end()) {
        all_conn_names_.erase(conn->name());
        all_sessions_.erase(conn_it->second);
      }

      TunnelMap::iterator tunnel_it = all_tunnels.find(conn->name());
      if (tunnel_it != all_tunnels.end()) {
        tunnel_it->second->disconnect();
        all_tunnels.erase(tunnel_it);
      }
    }
  }

  void OnKCPSessionClose(const KCPSessionPtr& kcp_session) {
    uint32_t session_id = kcp_session->session_id();
    SessionMap::iterator session_iterator = all_sessions_.find(session_id);
    if (session_iterator != all_sessions_.end()) {
      const muduo::net::TcpConnectionPtr& conn =
          (session_iterator->second).conn;
      conn->shutdown();
    }
    LOG_INFO << "OnKCPSessionClose";
  }

 private:
  void SendUDPMessage(const KCPSessionPtr& kcp_session,
                      muduo::net::Buffer* buf) {
    udp_server_.SendOrQueuePacket(buf, kcp_session->peer_address());
  }

 private:
  muduo::net::TcpServer tcp_server_;
  UDPServer udp_server_;

  SessionMap all_sessions_;
  ConnNameMap all_conn_names_;
  TunnelMap all_tunnels;
};

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  LOG_INFO << "FLAGS_target_ip: " << FLAGS_target_ip
           << ", FLAGS_target_port: " << FLAGS_target_port
           << ", FLAGS_tcp_server_ip: " << FLAGS_tcp_server_ip
           << ", FLAGS_tcp_server_port: " << FLAGS_tcp_server_port
           << ", FLAGS_udp_server_ip: " << FLAGS_udp_server_ip
           << ", FLAGS_udp_server_port: " << FLAGS_udp_server_port;

  muduo::g_logLevel = muduo::Logger::WARN;

  EventLoop loop;
  InetAddress tcp_server_addr(FLAGS_tcp_server_ip, FLAGS_tcp_server_port);
  InetAddress udp_server_addr(FLAGS_udp_server_ip, FLAGS_udp_server_port);

  KCPRelayBackend server(&loop, tcp_server_addr, udp_server_addr);

  server.Start();
  loop.loop();

  return 0;
}
