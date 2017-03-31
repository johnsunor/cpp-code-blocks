
#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>

#include "udp/udp_server.h"
#include "kcp/kcp_session.h"

const size_t kRttFrameLen = 2 * sizeof(int64_t);

using namespace muduo;
using namespace muduo::net;

class TestServer {
 public:
  struct SessionInfo {
    uint32_t session_id;
    uint32_t key;
    TcpConnectionPtr conn;
  };

  typedef std::map<uint32_t, SessionInfo> SessionMap;
  typedef std::map<muduo::string, uint32_t> ConnNameMap;

  TestServer(EventLoop* loop, const InetAddress& tcp_server_addr,
             const InetAddress& udp_server_addr)
      : tcp_server_(loop, tcp_server_addr, "TestServer"),
        udp_server_(loop, udp_server_addr) {
    tcp_server_.setConnectionCallback(
        boost::bind(&TestServer::OnTcpServerConnection, this, _1));

    udp_server_.set_message_callback(
        boost::bind(&TestServer::OnUDPServerMessage, this, _1, _2, _3));
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

        kcp_session->set_peer_address(peer_address);
        kcp_session->set_message_callback(
            boost::bind(&TestServer::OnKCPMessage, this, _1, _2));
        kcp_session->set_output_callback(
            boost::bind(&TestServer::SendUDPMessage, this, _1, _2));
        kcp_session->set_close_callback(
            boost::bind(&TestServer::OnKCPSessionClose, this, _1));

        conn->setContext(kcp_session);

        SendMetaData(MetaData::kAck, session_id, key, peer_address);
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
    }
  }

  void OnKCPSessionClose(const KCPSessionPtr& kcp_session) {
    SessionMap::iterator session_it =
        all_sessions_.find(kcp_session->session_id());
    if (session_it != all_sessions_.end()) {
      session_it->second.conn->shutdown();
    }
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
};

int main() {
  EventLoop loop;
  InetAddress tcp_server_addr(8090);
  InetAddress udp_server_addr(8091);
  TestServer server(&loop, tcp_server_addr, udp_server_addr);

  server.Start();
  loop.loop();

  return 0;
}
