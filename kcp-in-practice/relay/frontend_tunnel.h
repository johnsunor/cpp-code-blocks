#ifndef FRONTEND_TUNNEL_H_
#define FRONTEND_TUNNEL_H_

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "udp/udp_client.h"
#include "kcp/kcp_session.h"

const size_t kSessionInitFrameLen = sizeof(uint16_t) + 2 * sizeof(uint32_t);

class FrontendTunnel : public boost::enable_shared_from_this<FrontendTunnel>,
                       boost::noncopyable {
 public:
  FrontendTunnel(muduo::net::EventLoop* loop,
                 const muduo::net::InetAddress& backend_addr,
                 const muduo::net::TcpConnectionPtr& frontend_conn)
      : client_(loop, backend_addr, frontend_conn->name()),
        frontend_conn_(frontend_conn),
        udp_client_(loop) {
    LOG_INFO << "FrontendTunnel " << frontend_conn->peerAddress().toIpPort()
             << " <-> " << backend_addr.toIpPort();
  }

  ~FrontendTunnel() { LOG_INFO << "~FrontendTunnel"; }

  void setup() {
    client_.setConnectionCallback(boost::bind(
        &FrontendTunnel::onClientConnection, shared_from_this(), _1));
    client_.setMessageCallback(boost::bind(&FrontendTunnel::OnClientMessage,
                                           shared_from_this(), _1, _2, _3));
    frontend_conn_->setHighWaterMarkCallback(
        boost::bind(&FrontendTunnel::OnHighWaterMarkWeak,
                    boost::weak_ptr<FrontendTunnel>(shared_from_this()),
                    kServer, _1, _2),
        1024 * 1024);

    udp_client_.set_message_callback(
        boost::bind(&FrontendTunnel::OnUDPClientMessage, this, _1, _2));
  }

  void OnClientMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buf, muduo::Timestamp) {
    LOG_INFO << "conn: " << conn->name() << " recv bytes: " << buf->readableBytes();
    if (buf->readableBytes() == kSessionInitFrameLen) {
      uint16_t port = muduo::implicit_cast<uint16_t>(buf->readInt16());
      uint32_t session_id = muduo::implicit_cast<int>(buf->readInt32());
      uint32_t key = muduo::implicit_cast<int>(buf->readInt32());

      const muduo::net::InetAddress& peer_address = conn->peerAddress();

      LOG_INFO << "udp_server port: " << port << ", session_id: " << session_id;

      muduo::net::InetAddress udp_server_addr(peer_address.toIp(), port);
      assert(udp_client_.Connect(udp_server_addr) == 0);

      udp_client_.Start();

      const MetaData data = {MetaData::kSyn,
                             muduo::net::sockets::hostToNetwork32(session_id),
                             muduo::net::sockets::hostToNetwork32(key)};
      udp_client_.WriteOrQueuePacket(reinterpret_cast<const char*>(&data),
                                     sizeof(data));
      retry_timer_.timer_id = conn->getLoop()->runAfter(
          kRetryInterval, boost::bind(&FrontendTunnel::OnSynTimedOutWeak,
                                      shared_from_this(), session_id, key));
    } else {
      LOG_ERROR << "unexpected message len: " << buf->readableBytes();
    }
  }

  void OnUDPClientMessage(muduo::net::Buffer* buf, muduo::Timestamp) {
    assert(client_conn_);
    assert(buf->readableBytes() >= sizeof(MetaData));

    uint8_t kind = muduo::implicit_cast<uint8_t>(buf->readInt8());
    uint32_t session_id = muduo::implicit_cast<uint32_t>(buf->readInt32());
    uint32_t key = muduo::implicit_cast<uint32_t>(buf->readInt32());

    if (kind == MetaData::kAck) {
      if (client_conn_->getContext().empty()) {
        KCPSessionPtr kcp_session(new KCPSession(client_conn_->getLoop()));
        assert(kcp_session->Init(session_id, key, kFastModeKCPParams));

        kcp_session->set_message_callback(
            boost::bind(&FrontendTunnel::OnKCPMessage, this, _1, _2));
        kcp_session->set_output_callback(
            boost::bind(&FrontendTunnel::SendUDPMessage, this, _1, _2));

        frontend_conn_->startRead();
        frontend_conn_->setContext(kcp_session);
        client_conn_->setContext(kcp_session);
        client_conn_->getLoop()->cancel(retry_timer_.timer_id);
      }
    } else if (kind == MetaData::kPsh) {
      if (!client_conn_->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(client_conn_->getContext());
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
    LOG_INFO << "kcp session #" << kcp_session->session_id()
             << " recv kcp message bytes: " << buf->readableBytes();
    if (frontend_conn_) {
      frontend_conn_->send(buf);
    } else {
      buf->retrieveAll();
      abort();
    }
  }

  void connect() { client_.connect(); }

  void disconnect() {
    client_.disconnect();
    udp_client_.Disconnect();
  }

 private:
  void teardown() {
    client_.setConnectionCallback(muduo::net::defaultConnectionCallback);
    client_.setMessageCallback(muduo::net::defaultMessageCallback);
    if (frontend_conn_) {
      frontend_conn_->setContext(boost::any());
      frontend_conn_->shutdown();
    }
    client_conn_->setContext(boost::any());
    client_conn_.reset();

    udp_client_.Disconnect();
  }

  void onClientConnection(const muduo::net::TcpConnectionPtr& conn) {
    LOG_INFO << (conn->connected() ? "UP" : "DOWN");
    if (conn->connected()) {
      conn->setTcpNoDelay(true);
      client_conn_ = conn;
    } else {
      teardown();
    }
  }

  void OnSynTimedOut(uint32_t session_id, uint32_t key) {
    if (client_conn_ && udp_client_.IsConnected() &&
        retry_timer_.retry_times++ < kMaxRetryTimes) {
      const MetaData data = {MetaData::kSyn,
                             muduo::net::sockets::hostToNetwork32(session_id),
                             muduo::net::sockets::hostToNetwork32(key)};
      udp_client_.WriteOrQueuePacket(reinterpret_cast<const char*>(&data),
                                     sizeof(data));
      retry_timer_.timer_id = client_conn_->getLoop()->runAfter(
          kRetryInterval, boost::bind(&FrontendTunnel::OnSynTimedOutWeak,
                                      shared_from_this(), session_id, key));
    }
  }

  static void OnSynTimedOutWeak(
      const boost::weak_ptr<FrontendTunnel>& wk_frontend_tunnel,
      uint32_t session_id, uint32_t key) {
    boost::shared_ptr<FrontendTunnel> tunnel = wk_frontend_tunnel.lock();
    if (tunnel) {
      tunnel->OnSynTimedOut(session_id, key);
    }
  }

  void SendUDPMessage(const KCPSessionPtr& kcp_session,
                      muduo::net::Buffer* buf) {
    LOG_INFO << "kcp session #" << kcp_session->session_id()
             << " send udp message bytes: " << buf->readableBytes();
    if (udp_client_.IsConnected()) {
      udp_client_.WriteOrQueuePacket(buf);
    } else {
      LOG_WARN << "udp_client has disconnected";
    }
  }

  enum ServerClient { kServer, kClient };

  void onHighWaterMark(ServerClient which,
                       const muduo::net::TcpConnectionPtr& conn,
                       size_t bytes_to_sent) {
    LOG_INFO << (which == kServer ? "server" : "client") << " onHighWaterMark "
             << conn->name() << " bytes " << bytes_to_sent;

    if (which == kServer) {
      if (frontend_conn_->outputBuffer()->readableBytes() > 0) {
        client_conn_->stopRead();
        frontend_conn_->setWriteCompleteCallback(boost::bind(
            &FrontendTunnel::OnWriteCompleteWeak,
            boost::weak_ptr<FrontendTunnel>(shared_from_this()), kServer, _1));
      }
    } else {
      if (client_conn_->outputBuffer()->readableBytes() > 0) {
        frontend_conn_->stopRead();
        client_conn_->setWriteCompleteCallback(boost::bind(
            &FrontendTunnel::OnWriteCompleteWeak,
            boost::weak_ptr<FrontendTunnel>(shared_from_this()), kClient, _1));
      }
    }
  }

  static void OnHighWaterMarkWeak(
      const boost::weak_ptr<FrontendTunnel>& wkFrontendTunnel,
      ServerClient which, const muduo::net::TcpConnectionPtr& conn,
      size_t bytes_to_sent) {
    boost::shared_ptr<FrontendTunnel> tunnel = wkFrontendTunnel.lock();
    if (tunnel) {
      tunnel->onHighWaterMark(which, conn, bytes_to_sent);
    }
  }

  void OnWriteComplete(ServerClient which,
                       const muduo::net::TcpConnectionPtr& conn) {
    LOG_INFO << (which == kServer ? "server" : "client") << " OnWriteComplete "
             << conn->name();
    if (which == kServer) {
      client_conn_->startRead();
      frontend_conn_->setWriteCompleteCallback(
          muduo::net::WriteCompleteCallback());
    } else {
      frontend_conn_->startRead();
      client_conn_->setWriteCompleteCallback(
          muduo::net::WriteCompleteCallback());
    }
  }

  static void OnWriteCompleteWeak(
      const boost::weak_ptr<FrontendTunnel>& wkFrontendTunnel,
      ServerClient which, const muduo::net::TcpConnectionPtr& conn) {
    boost::shared_ptr<FrontendTunnel> tunnel = wkFrontendTunnel.lock();
    if (tunnel) {
      tunnel->OnWriteComplete(which, conn);
    }
  }

 private:
  static const uint32_t kRetryInterval = 1;
  static const uint32_t kMaxRetryTimes = 5;

  struct RetryTimer {
    RetryTimer() : retry_times(0) {}
    uint32_t retry_times;
    muduo::net::TimerId timer_id;
  };

  muduo::net::TcpClient client_;
  muduo::net::TcpConnectionPtr frontend_conn_;
  muduo::net::TcpConnectionPtr client_conn_;
  UDPClient udp_client_;
  RetryTimer retry_timer_;
  muduo::net::Buffer buf;
};
typedef boost::shared_ptr<FrontendTunnel> FrontendTunnelPtr;

#endif
