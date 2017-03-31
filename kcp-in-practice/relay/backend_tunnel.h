#ifndef BACKEND_TUNNEL_H_
#define BACKEND_TUNNEL_H_

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "kcp/kcp_session.h"

class BackendTunnel : public boost::enable_shared_from_this<BackendTunnel>,
                      boost::noncopyable {
 public:
  BackendTunnel(muduo::net::EventLoop* loop,
                const muduo::net::InetAddress& target_addr,
                const muduo::net::TcpConnectionPtr& backend_conn)
      : client_(loop, target_addr, backend_conn->name()),
        backend_conn_(backend_conn) {
    LOG_INFO << "BackendTunnel " << backend_conn->peerAddress().toIpPort()
             << " <-> " << target_addr.toIpPort();
  }

  ~BackendTunnel() { LOG_INFO << "~BackendTunnel"; }

  void setup() {
    client_.setConnectionCallback(boost::bind(
        &BackendTunnel::OnClientConnection, shared_from_this(), _1));
    client_.setMessageCallback(boost::bind(&BackendTunnel::OnClientMessage,
                                           shared_from_this(), _1, _2, _3));
    backend_conn_->setHighWaterMarkCallback(
        boost::bind(&BackendTunnel::OnHighWaterMarkWeak,
                    boost::weak_ptr<BackendTunnel>(shared_from_this()), kServer,
                    _1, _2),
        1024 * 1024);
  }

  void set_target_connection_callback(
      const muduo::net::ConnectionCallback& callback) {
    target_connection_callback_ = callback;
  }

  void connect() { client_.connect(); }

  void disconnect() {
    client_.disconnect();
    // backend_conn_.reset();
  }

 private:
  void teardown() {
    client_.setConnectionCallback(muduo::net::defaultConnectionCallback);
    client_.setMessageCallback(muduo::net::defaultMessageCallback);
    if (backend_conn_) {
      if (!backend_conn_->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(backend_conn_->getContext());
        kcp_session->set_context(boost::any());
      }
      backend_conn_->shutdown();
    }
    client_conn_.reset();
  }

  void OnClientConnection(const muduo::net::TcpConnectionPtr& conn) {
    LOG_INFO << (conn->connected() ? "UP" : "DOWN");
    if (conn->connected()) {
      conn->setTcpNoDelay(true);
      client_conn_ = conn;
      if (!backend_conn_->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(backend_conn_->getContext());
        kcp_session->set_context(conn);
      }
    } else {
      teardown();
    }

    if (target_connection_callback_) {
      target_connection_callback_(conn);
    }
  }

  void OnClientMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buf, muduo::Timestamp) {
    LOG_INFO << conn->name() << "recv " << buf->readableBytes()
             << " bytes from target";
    if (backend_conn_) {
      if (!backend_conn_->getContext().empty()) {
        const KCPSessionPtr& kcp_session =
            boost::any_cast<const KCPSessionPtr&>(backend_conn_->getContext());
        kcp_session->Send(buf);
      }
    }
  }

  enum ServerClient { kServer, kClient };

  void OnHighWaterMark(ServerClient which,
                       const muduo::net::TcpConnectionPtr& conn,
                       size_t bytes_to_sent) {
    LOG_INFO << (which == kServer ? "server" : "client") << " OnHighWaterMark "
             << conn->name() << " bytes " << bytes_to_sent;

    if (which == kServer) {
      if (backend_conn_->outputBuffer()->readableBytes() > 0) {
        client_conn_->stopRead();
        backend_conn_->setWriteCompleteCallback(boost::bind(
            &BackendTunnel::OnWriteCompleteWeak,
            boost::weak_ptr<BackendTunnel>(shared_from_this()), kServer, _1));
      }
    } else {
      if (client_conn_->outputBuffer()->readableBytes() > 0) {
        backend_conn_->stopRead();
        client_conn_->setWriteCompleteCallback(boost::bind(
            &BackendTunnel::OnWriteCompleteWeak,
            boost::weak_ptr<BackendTunnel>(shared_from_this()), kClient, _1));
      }
    }
  }

  static void OnHighWaterMarkWeak(
      const boost::weak_ptr<BackendTunnel>& wkBackendTunnel, ServerClient which,
      const muduo::net::TcpConnectionPtr& conn, size_t bytes_to_sent) {
    boost::shared_ptr<BackendTunnel> tunnel = wkBackendTunnel.lock();
    if (tunnel) {
      tunnel->OnHighWaterMark(which, conn, bytes_to_sent);
    }
  }

  void OnWriteComplete(ServerClient which,
                       const muduo::net::TcpConnectionPtr& conn) {
    LOG_INFO << (which == kServer ? "server" : "client") << " OnWriteComplete "
             << conn->name();
    if (which == kServer) {
      client_conn_->startRead();
      backend_conn_->setWriteCompleteCallback(
          muduo::net::WriteCompleteCallback());
    } else {
      backend_conn_->startRead();
      client_conn_->setWriteCompleteCallback(
          muduo::net::WriteCompleteCallback());
    }
  }

  static void OnWriteCompleteWeak(
      const boost::weak_ptr<BackendTunnel>& wkBackendTunnel, ServerClient which,
      const muduo::net::TcpConnectionPtr& conn) {
    boost::shared_ptr<BackendTunnel> tunnel = wkBackendTunnel.lock();
    if (tunnel) {
      tunnel->OnWriteComplete(which, conn);
    }
  }

 private:
  muduo::net::TcpClient client_;
  muduo::net::TcpConnectionPtr backend_conn_;
  muduo::net::TcpConnectionPtr client_conn_;

  muduo::net::ConnectionCallback target_connection_callback_;
};
typedef boost::shared_ptr<BackendTunnel> BackendTunnelPtr;

#endif
