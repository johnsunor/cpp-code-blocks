
#include <map>

#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/InetAddress.h>

//#include "codec/codec.h"
//#include "codec/dispatcher.h"

#include "udp/udp_client.h"
#include "kcp/kcp_session.h"

using namespace muduo;
using namespace muduo::net;

const double kClientSendMyTimeInterval = 1;         // 1s

const int kSessionInitFrameLen = sizeof(uint16_t) + sizeof(int);
const int kRttFrameLen = 2 * sizeof(int64_t);

class TestClient {
 public:
  TestClient(EventLoop* loop, const InetAddress& tcp_server_addr)
      : tcp_client_(loop, tcp_server_addr, "TestClient"), udp_client_(loop) {
    tcp_client_.setConnectionCallback(
        boost::bind(&TestClient::OnTcpClientConnection, this, _1));
    tcp_client_.setMessageCallback(
        boost::bind(&TestClient::OnTcpClientMessage, this, _1, _2, _3));

    udp_client_.set_message_callback(
        boost::bind(&TestClient::OnUDPClientMessage, this, _1, _2));

    loop->runEvery(kClientSendMyTimeInterval,
                   boost::bind(&TestClient::SendMyTime, this));

    loop->runEvery(30, boost::bind(&TestClient::PrintRttStats, this));
  }

  void PrintRttStats() {
    for (std::map<int,int>::iterator it = rtt_stats_.begin();
         it != rtt_stats_.end(); ++it) {
      LOG_WARN << "RTT: " << it->first << ", stat times: " << it->second;
    }
  }

  void SendMyTime() {
    if (tcp_conn_ && !tcp_conn_->getContext().empty()) {
      int64_t message[2] = {0, 0};
      message[0] = Timestamp::now().microSecondsSinceEpoch() / 1000;
      const KCPSessionPtr& kcp_session =
          boost::any_cast<const KCPSessionPtr&>(tcp_conn_->getContext());

      kcp_session->Send(reinterpret_cast<const char*>(message), kRttFrameLen);
    }
  }

  void Start() { tcp_client_.connect(); }

  void OnUDPClientMessage(muduo::net::Buffer* buf,
                          muduo::Timestamp receive_time) {
    if (buf->readableBytes() < sizeof(KCPSession::MetaData)) {
      LOG_ERROR << "buf size is too short";
      return;
    }

    uint8_t kind = implicit_cast<uint8_t>(buf->readInt8());
    int session_id = buf->readInt32();
    LOG_INFO << "kcp session #" << session_id << " recved udp message at time: "
             << receive_time.microSecondsSinceEpoch() / 1000;

    if (kind == KCPSession::MetaData::ACK) {
      if (buf->readableBytes() != 0) {
        LOG_ERROR << "invalid meta data ack";
        return;
      }

      KCPSessionPtr kcp_session(new KCPSession(tcp_conn_->getLoop()));
      if (!kcp_session->Init(session_id, kFastModeKCPParams)) {
        tcp_conn_->shutdown();
        return;
      }
      kcp_session->set_message_callback(
          boost::bind(&TestClient::OnKCPMessage, this, _1, _2));
      kcp_session->set_output_callback(
          boost::bind(&TestClient::SendUDPMessage, this, _1, _2));

      tcp_conn_->setContext(kcp_session);
    } else if (kind == KCPSession::MetaData::PSH) {
      if (tcp_conn_->getContext().empty()) {
        LOG_FATAL << "impossible - kcp_session not exists";
        return;
      }

      const KCPSessionPtr& kcp_session =
          boost::any_cast<const KCPSessionPtr&>(tcp_conn_->getContext());
      if (kcp_session->session_id() != session_id) {
        LOG_FATAL << "impossible - kcp_session already exists, but "
                     "session_id invalid, client session id: ,"
                  << kcp_session->session_id()
                  << " server session id = " << session_id;
      }

      kcp_session->Feed(buf->peek(), buf->readableBytes());
    } else {
      LOG_ERROR << "recved unexpected kind: " << kind;
    }
  }

  void OnKCPMessage(const KCPSessionPtr& kcp_session, muduo::net::Buffer* buf) {
    if (buf->readableBytes() == kRttFrameLen) {
      const int64_t* message = reinterpret_cast<const int64_t*>(buf->peek());
      int64_t send = message[0];
      int64_t their = message[1];
      int64_t back = muduo::Timestamp::now().microSecondsSinceEpoch() / 1000;
      int64_t mine = (back + send) / 2;
      LOG_INFO << "round trip " << back - send << " clock error "
               << their - mine;
      int64_t rtt = back - send;
      if (rtt_stats_.find(rtt) == rtt_stats_.end()) {
        rtt_stats_[rtt] = 1;
      } else {
        rtt_stats_[rtt]++;
      }
    } else {
      LOG_ERROR << "recved unexpected packet length: " << buf->readableBytes()
                << ", session_id: " << kcp_session->session_id();
    }
  }

  void OnTcpClientMessage(const muduo::net::TcpConnectionPtr& conn,
                          muduo::net::Buffer* buf, muduo::Timestamp) {
    if (buf->readableBytes() == implicit_cast<size_t>(kSessionInitFrameLen)) {
      uint16_t port = implicit_cast<uint16_t>(buf->readInt16());
      int session_id = implicit_cast<int>(buf->readInt32());
      const muduo::net::InetAddress& peer_address = conn->peerAddress();

      LOG_INFO << "udp_server port: " << port << ", session_id: " << session_id;

      muduo::net::InetAddress udp_server_addr(peer_address.toIp(), port);
      if (udp_client_.Connect(udp_server_addr) < 0) {
        LOG_ERROR << "connect to udp_server_addr: "
                  << udp_server_addr.toIpPort() << " failed";
        return;
      }
      udp_client_.Start();

      KCPSession::MetaData data = {
          KCPSession::MetaData::SYN,
          static_cast<int>(sockets::hostToNetwork32(session_id))};
      udp_client_.WriteOrQueuePcket(reinterpret_cast<const char*>(&data),
                                    sizeof(data));
    } else {
      LOG_ERROR << "unexpected message len: " << buf->readableBytes();
    }
  }

  void OnTcpClientConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
      conn->setTcpNoDelay(true);
      tcp_conn_ = conn;
    } else {
      conn->setContext(boost::any());
      tcp_conn_.reset();
    }
  }

 private:
  void SendUDPMessage(const KCPSessionPtr& kcp_session,
                      muduo::net::Buffer* buf) {
    LOG_INFO << "kcp session #" << kcp_session->session_id()
             << " send udp message with bytes: " << buf->readableBytes();
    udp_client_.WriteOrQueuePcket(buf->peek(), buf->readableBytes());
  }

  void OnKCPSessionClose(const KCPSessionPtr& kcp_session) {
    LOG_INFO << "kcp session #" << kcp_session->session_id() << " need close";
    tcp_client_.disconnect();
  }

 private:
  muduo::net::TcpClient tcp_client_;
  UDPClient udp_client_;
  TcpConnectionPtr tcp_conn_;

  std::map<int, int> rtt_stats_;
};

int main() {
  using namespace muduo;
  using namespace muduo::net;

  g_logLevel = muduo::Logger::WARN;

  EventLoop loop;
  InetAddress server_addr(8090);
  TestClient client(&loop, server_addr);

  client.Start();
  loop.loop();

  return 0;
}
