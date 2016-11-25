
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>

//#include "codec/codec.h"
//#include "codec/dispatcher.h"

#include "udp_server.h"
#include "kcp_session.h"

using namespace muduo;
using namespace muduo::net;

const double kServerUpdateSessionInterval = 0.01;  // 0.01s/10ms

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

    // loop->runEvery(kServerUpdateSessionInterval,
    // boost::bind(&TestServer::UpdateSession, this));
  }

  void Start() {
    tcp_server_.start();
    udp_server_.Start();
  }

  void OnUDPServerMessage(muduo::net::Buffer* buf,
                          muduo::Timestamp receive_time,
                          const muduo::net::InetAddress& peer_address) {
    if (buf->readableBytes() < sizeof(KCPSession::MetaData)) {
      LOG_ERROR << "unexpected buf length: " << buf->readableBytes();
      return;
    }

    uint8_t kind = implicit_cast<uint8_t>(buf->readInt8());

    int session_id = buf->readInt32();
    SessionIdMap::iterator it = all_sessions_.find(session_id);
    if (it == all_sessions_.end()) {
      LOG_ERROR << "session not exists, client session id: " << session_id
                << " from " << peer_address.toIpPort();
      return;
    }

    const TcpConnectionPtr& conn = all_conns_[it->second];

    if (kind == KCPSession::MetaData::SYN) {
      if (buf->readableBytes() != 0) {
        LOG_ERROR << "unexpected buf length: " << buf->readableBytes()
                  << " in SYN meta data";
        return;
      }

      if (!conn->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(conn->getContext());
        if (kcp_session->session_id() != session_id) {
          LOG_FATAL << "impossible - kcp_session already exists, but "
                       "session id invalid, server session id: "
                    << kcp_session->session_id()
                    << ", client session id = " << session_id;
        }
        LOG_INFO << "recved duplicate SYN meta data, client session id: "
                 << session_id << " from " << peer_address.toIpPort();

        KCPSession::MetaData data = {
            KCPSession::MetaData::ACK,
            static_cast<int>(sockets::hostToNetwork32(session_id))};
        udp_server_.SendOrQueuePacket(reinterpret_cast<const char*>(&data),
                                      sizeof(data), peer_address);
        return;
      }

      KCPSessionPtr kcp_session(new KCPSession(conn->getLoop()));
      if (!kcp_session->Init(session_id, kFastModeKCPParams)) {
        conn->shutdown();
        return;
      }

      kcp_session->set_context(peer_address);
      kcp_session->set_message_callback(
          boost::bind(&TestServer::OnKCPMessage, this, _1, _2));
      kcp_session->set_output_callback(
          boost::bind(&TestServer::SendUDPMessage, this, _1, _2));
      kcp_session->set_close_callback(
          boost::bind(&TestServer::OnKCPSessionClose, this, _1));

      KCPSession::MetaData data = {
          KCPSession::MetaData::ACK,
          static_cast<int>(sockets::hostToNetwork32(session_id))};
      udp_server_.SendOrQueuePacket(reinterpret_cast<const char*>(&data),
                                    sizeof(data), peer_address);

      conn->setContext(kcp_session);
    } else if (kind == KCPSession::MetaData::PSH) {
      LOG_INFO << "UDP message recved time: "
               << receive_time.microSecondsSinceEpoch() / 1000;

      if (conn->getContext().empty()) {
        LOG_FATAL << "impossible - kcp_session not exists, client session id: "
                  << session_id;
        return;
      }

      const KCPSessionPtr& kcp_session =
          boost::any_cast<const KCPSessionPtr&>(conn->getContext());
      if (kcp_session->session_id() != session_id) {
        LOG_FATAL << "impossible - kcp_session already exists, but "
                     "session id invalid, server session id: "
                  << kcp_session->session_id()
                  << ", client session id = " << session_id;
      }

      kcp_session->Feed(buf->peek(), buf->readableBytes());
    } else {
      LOG_ERROR << "recved unexpected kind: " << kind << " from "
                << peer_address.toIpPort();
    }
  }

  void OnKCPMessage(const KCPSessionPtr& kcp_session, muduo::net::Buffer* buf) {
    const int kRttFrameLen = 2 * sizeof(int64_t);
    if (buf->readableBytes() == kRttFrameLen) {
      const int64_t* message = reinterpret_cast<const int64_t*>(buf->peek());
      int64_t new_message[2] = {message[0], 0};
      new_message[1] = muduo::Timestamp::now().microSecondsSinceEpoch() / 1000;
      kcp_session->Send(reinterpret_cast<const char*>(new_message),
                        kRttFrameLen);
      LOG_INFO << "client clock: " << new_message[0]
               << ", server clock: " << new_message[1];
    } else {
      LOG_ERROR << "recved unexpected packet length: " << buf->readableBytes()
                << ", session_id: " << kcp_session->session_id();
    }
  }

  void OnTcpServerMessage(const muduo::net::TcpConnectionPtr& conn,
                          muduo::net::Buffer* buf, muduo::Timestamp) {
    (void)conn;
    (void)buf;
  }

  void SendSessionInitInfo(const muduo::net::TcpConnectionPtr& conn,
                           int session_id) {
    muduo::net::InetAddress addr;
    int result = udp_server_.GetLocalAddress(&addr);
    if (result < 0) {
      return;
    }

    muduo::net::Buffer buf;
    buf.appendInt16(addr.toPort());
    buf.appendInt32(session_id);
    conn->send(&buf);

    LOG_INFO << "SendSessionInitInfo - session_id: " << session_id
             << ", port:" << addr.toIpPort();
  }

  void OnTcpServerConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
      conn->setTcpNoDelay(true);

      int session_id =
          KCPSessionIdInitSingleton::GetInstance().GetNextSessionId();

      if (session_id == kInvalidSessionId) {
        conn->shutdown();
        return;
      }

      SendSessionInitInfo(conn, session_id);

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

  void OnKCPSessionClose(const KCPSessionPtr& kcp_session) {
    int session_id = kcp_session->session_id();
    SessionIdMap::iterator session_iterator = all_sessions_.find(session_id);
    if (session_iterator != all_sessions_.end()) {
      const muduo::net::TcpConnectionPtr& conn =
          all_conns_[session_iterator->second];
      conn->shutdown();
    }
  }

 private:
  void SendUDPMessage(const KCPSessionPtr& kcp_session,
                      muduo::net::Buffer* buf) {
    if (!kcp_session->context().empty()) {
      const muduo::net::InetAddress& peer_address =
          boost::any_cast<muduo::net::InetAddress>(kcp_session->context());
      udp_server_.SendOrQueuePacket(buf->peek(), buf->readableBytes(),
                                    peer_address);
    }
  }

 private:
  typedef std::map<string, TcpConnectionPtr> TcpConnMap;
  typedef std::map<int, string> SessionIdMap;

  muduo::net::TcpServer tcp_server_;
  UDPServer udp_server_;

  // ProtobufCodec tcp_codec_;
  // ProtobufDispatcher tcp_dispatcher_;

  TcpConnMap all_conns_;
  SessionIdMap all_sessions_;
};

int main() {
  using namespace muduo;
  using namespace muduo::net;

  EventLoop loop;
  InetAddress tcp_server_addr(8090);
  InetAddress udp_server_addr(8091);
  TestServer server(&loop, tcp_server_addr, udp_server_addr);

  server.Start();
  loop.loop();

  return 0;
}
