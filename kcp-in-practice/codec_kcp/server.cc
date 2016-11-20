
#include "udp_server.h"
#include "kcp_session.h"

#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>

using namespace muduo;
using namespace muduo::net;

class TestServer {
 public:
  TestServer(EventLoop* loop, const InetAddress& tcp_server_addr,
             const InetAddress& udp_server_addr)
      : tcp_server_(loop, tcp_server_addr, "TestServer"),
        udp_server_(loop, udp_server_addr) {
    tcp_server_.setConnectionCallback(
        boost::bind(&TestServer::OnTcpServerConnection, this, _1));
    tcp_server_.setMessageCallback(
        boost::bind(&TestServer::OnTcpServerMessage, this, _1, _2, _3));

    udp_server_.set_message_callback(
        boost::bind(&TestServer::OnUDPServerMessage, this, _1, _2, _3));
  }

  void OnUDPServerMessage(muduo::net::Buffer* buf,
                          muduo::Timestamp receieve_time,
                          const muduo::net::InetAddress& peer_address) {
    if (buf->readableBytes() < sizeof(MetaData)) {
      LOG_ERROR << "buf size is too short";
      return;
    }

    uint8_t kind = implicit_cast<uint8_t>(buf->readInt8());

    int session_id = buf->readInt32();
    SessionIdMap::iterator it = all_sessions_.find(session_id);
    if (it == all_sessions_.end()) {
      LOG_ERROR << "session not exists, session_id =  " << session_id;
      return;
    }

    const TcpConnectionPtr& conn = all_conns_[it->second];

    if (kind == KCPSession::MetaData::SYN) {
      if (buf->readableBytes() != 0) {
        LOG_ERROR << "invalid meta data syn";
        return;
      }

      if (!conn->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(conn->getContext());
        if (kcp_session->session_id() != session_id) {
          LOG_FATAL << "impossible - kcp_session already exists, but "
                       "session_id invalid, kcp_session->session_id() =,"
                    << kcp_session->session_id()
                    << " syn session_id = " << session_id;
        }

        LOG_INFO << "recv duplicate syn, session_id = " << session_id;
        return;
      }

      KCPSessionPtr kcp_session(new KCPSession);

      if (!kcp_session->Init(session_id, kFastModeKCPParams)) {
        conn->shutdown();
      }

      kcp_session->set_context(peer_address);

      conn->setContext(kcp_session);
    } else if (kind == KCPSession::MetaData::PSH) {
      if (conn->getContext().empty()) {
        LOG_FATAL << "impossible - kcp_session not exists";
        return;
      }

      const KCPSessionPtr& kcp_session =
          boost::any_cast<const KCPSessionPtr&>(conn->getContext());
      if (kcp_session->session_id() != session_id) {
        LOG_FATAL << "impossible - kcp_session already exists, but "
                     "session_id invalid, kcp_session->session_id() =,"
                  << kcp_session->session_id()
                  << " syn session_id = " << session_id;
      }

      kcp_session->Feed(buf->peek(), buf->readableBytes());
    }
  }

  void OnTcpServerMessage(const muduo::net::TcpConnectionPtr& conn,
                          muduo::net::Buffer* buf, muduo::Timestamp) {
    (void)conn;
    (void)buf;
  }

  void OnTcpServerConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
      conn->setTcpNoDelay(true);

      int session_id =
          KCPSessionIdInitSingleton::GetInstance().GetNextSessionId();

      if (session_id == kInvalidSessionId) {
        conn->shutdown();
      }

      MetaData data = {MetaData::SYN, session_id};
      conn->send(&data, sizeof(data));

      all_conns_[conn->name()] = conn;
      all_sessions_[session_id] = conn->name();
    } else {
      if (!conn->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(conn->getContext());

        KCPSessionIdInitSingleton::GetInstance().ReleaseSessionId(
            kcp_session->session_id());
        all_sessions_.erase(kcp_session->session_id());

        conn->setContext(boost::any());
      }

      all_conns_.erase(conn->name());
    }
  }

  void SendMessage(const KCPSessionPtr& kcp_session, muduo::net::Buffer* buf) {
    if (!kcp_session->context().empty()) {
      const muduo::net::InetAddress& peer_address =
          boost::any_cast<InetAddress>(kcp_session->context());
      udp_server_.SendOrQueuePcket(buf->peek(), buf->readableBytes(),
                                   peer_address);
    }
  }

 private:
  TcpServer tcp_server_;
  UDPServer udp_server_;

  typedef std::map<string, TcpConnectionPtr> TcpConnMap;
  typedef std::map<int, string> SessionIdMap;

  TcpConnMap all_conns_;
  SessionIdMap all_sessions_;
};

int main() {}
