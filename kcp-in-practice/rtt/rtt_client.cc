
#include <map>

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/InetAddress.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>

#include "udp/udp_client.h"
#include "kcp/kcp_session.h"

using namespace muduo;
using namespace muduo::net;

const double kClientSendMyTimeInterval = 1.0;  // 1s

const size_t kSessionInitFrameLen = sizeof(uint16_t) + 2 * sizeof(uint32_t);
const size_t kRttFrameLen = 2 * sizeof(int64_t);

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
    for (std::map<int, int>::iterator it = rtt_stats_.begin();
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

      LOG_INFO << "send my time: " << message[0];
    }
  }

  void Start() { tcp_client_.connect(); }

  void OnUDPClientMessage(muduo::net::Buffer* buf, muduo::Timestamp) {
    assert(tcp_conn_);
    assert(buf->readableBytes() >= sizeof(MetaData));

    uint8_t kind = muduo::implicit_cast<uint8_t>(buf->readInt8());
    uint32_t session_id = muduo::implicit_cast<uint32_t>(buf->readInt32());
    uint32_t key = muduo::implicit_cast<uint32_t>(buf->readInt32());

    if (kind == MetaData::kAck) {
      if (tcp_conn_->getContext().empty()) {
        KCPSessionPtr kcp_session(new KCPSession(tcp_conn_->getLoop()));
        assert(kcp_session->Init(session_id, key, kFastModeKCPParams));

        kcp_session->set_send_no_delay(true);
        kcp_session->set_message_callback(
            boost::bind(&TestClient::OnKCPMessage, this, _1, _2));
        kcp_session->set_output_callback(
            boost::bind(&TestClient::SendUDPMessage, this, _1, _2));

        tcp_conn_->setContext(kcp_session);
      }
    } else if (kind == MetaData::kPsh) {
      if (!tcp_conn_->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(tcp_conn_->getContext());
        if (session_id == kcp_session->session_id() &&
            key == kcp_session->key()) {
          kcp_session->Feed(buf);
        }
      }
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
    if (buf->readableBytes() == kSessionInitFrameLen) {
      uint16_t port = implicit_cast<uint16_t>(buf->readInt16());
      uint32_t session_id = implicit_cast<uint32_t>(buf->readInt32());
      uint32_t key = implicit_cast<uint32_t>(buf->readInt32());

      const muduo::net::InetAddress& peer_address = conn->peerAddress();

      LOG_INFO << "udp_server port: " << port << ", session_id: " << session_id;

      muduo::net::InetAddress udp_server_addr(peer_address.toIp(), port);
      assert(udp_client_.Connect(udp_server_addr) == 0);

      udp_client_.Start();

      const MetaData data = {MetaData::kSyn,
                             sockets::hostToNetwork32(session_id),
                             sockets::hostToNetwork32(key)};
      udp_client_.WriteOrQueuePacket(reinterpret_cast<const char*>(&data),
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
    if (udp_client_.IsConnected()) {
      udp_client_.WriteOrQueuePacket(buf->peek(), buf->readableBytes());
    } else {
      LOG_WARN << "udp_client has disconnected";
    }
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
  g_logLevel = muduo::Logger::INFO;

  EventLoop loop;
  InetAddress server_addr(8090);
  TestClient client(&loop, server_addr);

  client.Start();
  loop.loop();

  return 0;
}
