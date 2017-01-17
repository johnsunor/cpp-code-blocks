#ifndef TCPRELAY_TUNNEL_H_
#define TCPRELAY_TUNNEL_H_

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>

#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "encryptor_openssl.h"

#include "utils/string_utils.h"

class ServerConnMeta : boost::noncopyable {
 public:
  typedef muduo::net::Buffer BufferType;

  enum ReqState {
    STATE_REQ_ADDR_TYPE,
    STATE_REQ_DST_ADDR_VARLEN,
    STATE_REQ_DST_ADDR,
    STATE_REQ_DST_PORT,
    STATE_REQ_DONE
  };

  enum AddrType { ADDR_TYPE_IPV4 = 1, ADDR_TYPE_HOST = 3, ADDR_TYPE_IPV6 = 4 };

  ServerConnMeta(const muduo::net::TcpConnectionPtr& conn_ptr,
                 const crypto::EncryptorPtr& decryptor_ptr)
      : conn_(conn_ptr),
        decryptor_(decryptor_ptr),
        state_(STATE_REQ_ADDR_TYPE) {}

  bool Decrypt(const BufferType* data) {
    assert(data != NULL);
    return decryptor_->Update(data, &buf_);
  }

  bool Decrypt(const char* data, int len) {
    assert(data != NULL && len > 0);
    return decryptor_->Update(data, len, &buf_);
  }

  const muduo::net::TcpConnectionPtr& conn() const { return conn_; }
  const crypto::EncryptorPtr& decryptor() const { return decryptor_; }

  const BufferType* buf() const { return &buf_; }
  BufferType* mutable_buf() { return &buf_; }

  ReqState state() const { return state_; }
  void set_state(ReqState val) { state_ = val; }

  const muduo::string& dst_hostname() const { return dst_hostname_; }
  void set_dst_hostname(const muduo::string& val) { dst_hostname_ = val; }

  uint8_t dst_addr_len() const { return dst_addr_len_; }
  void set_dst_addr_len(uint8_t val) { dst_addr_len_ = val; }

  uint16_t dst_port() const { return dst_port_; }
  void set_dst_port(uint16_t val) { dst_port_ = val; }

  bool ParseRequest() {
    size_t need_bytes = 0;

    while (buf_.readableBytes() > need_bytes) {
      switch (this->state()) {
        case STATE_REQ_ADDR_TYPE: {
          uint8_t addr_type = static_cast<uint8_t>(buf_.readInt8());
          if (addr_type == ADDR_TYPE_HOST) {
            this->set_state(STATE_REQ_DST_ADDR_VARLEN);
          } else if (addr_type == ADDR_TYPE_IPV4) {
            return false;  // unsupported
          } else if (addr_type == ADDR_TYPE_IPV6) {
            return false;  // unsupported
          }
          break;
        }

        case STATE_REQ_DST_ADDR_VARLEN: {
          uint8_t addr_len = static_cast<uint8_t>(buf_.readInt8());
          this->set_dst_addr_len(addr_len);
          this->set_state(STATE_REQ_DST_ADDR);
          need_bytes = addr_len;
          break;
        }

        case STATE_REQ_DST_ADDR: {
          uint8_t addr_len = this->dst_addr_len();
          muduo::string dst_hostname_val(buf_.retrieveAsString(addr_len));
          this->set_dst_hostname(dst_hostname_val);
          this->set_state(STATE_REQ_DST_PORT);
          need_bytes = sizeof(uint16_t);
          break;
        }

        case STATE_REQ_DST_PORT: {
          uint16_t port = static_cast<uint16_t>(buf_.readInt16());
          this->set_dst_port(port);
          this->set_state(STATE_REQ_DONE);
          return true;
        }

        default: { return false; }
      }
    }

    return false;
  }

 private:
  const muduo::net::TcpConnectionPtr conn_;
  const crypto::EncryptorPtr decryptor_;

  ReqState state_;
  BufferType buf_;

  muduo::string dst_hostname_;
  uint8_t dst_addr_len_;
  uint16_t dst_port_;
};

typedef boost::shared_ptr<ServerConnMeta> ServerConnMetaPtr;

class Tunnel : public boost::enable_shared_from_this<Tunnel>,
               boost::noncopyable {
 public:
  Tunnel(muduo::net::EventLoop* loop, const muduo::net::InetAddress& serverAddr,
         const ServerConnMetaPtr& server_conn_meta,
         const crypto::EncryptorPtr& encryptor)
      : sent_iv_(false),
        server_conn_meta_(server_conn_meta),
        server_conn_(server_conn_meta->conn()),
        client_(loop, serverAddr, server_conn_meta->conn()->name()),
        encryptor_(encryptor) {
    LOG_INFO << "Tunnel " << server_conn_->peerAddress().toIpPort() << " <-> "
             << serverAddr.toIpPort();
  }

  ~Tunnel() { LOG_INFO << "~Tunnel"; }

  void setup() {
    client_.setConnectionCallback(
        boost::bind(&Tunnel::onClientConnection, shared_from_this(), _1));
    client_.setMessageCallback(
        boost::bind(&Tunnel::onClientMessage, shared_from_this(), _1, _2, _3));
    server_conn_->setHighWaterMarkCallback(
        boost::bind(&Tunnel::onHighWaterMarkWeak,
                    boost::weak_ptr<Tunnel>(shared_from_this()), _1, _2),
        10 * 1024 * 1024);
  }

  void teardown() {
    client_.setConnectionCallback(muduo::net::defaultConnectionCallback);
    client_.setMessageCallback(muduo::net::defaultMessageCallback);
    if (server_conn_) {
      server_conn_->setContext(boost::any());
      server_conn_->shutdown();
    }
  }

  void connect() { client_.connect(); }

  void disconnect() {
    client_.disconnect();
    // server_conn_.reset();
  }

  void onClientConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
      conn->setTcpNoDelay(true);
      conn->setHighWaterMarkCallback(
          boost::bind(&Tunnel::onHighWaterMarkWeak,
                      boost::weak_ptr<Tunnel>(shared_from_this()), _1, _2),
          100 * 1024 * 1024);
      server_conn_->setContext(conn);

      muduo::net::Buffer* buf = server_conn_meta_->mutable_buf();
      if (buf->readableBytes() > 0) {
        conn->send(buf);
      }
    } else {
      teardown();
    }
  }

  void onClientMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buf, muduo::Timestamp) {
    if (server_conn_) {
      if (!sent_iv_) {
        sent_iv_ = true;
        data_buf_.append(encryptor_->iv());
      }
      bool ok = encryptor_->Update(
          buf->peek(), static_cast<int>(buf->readableBytes()), &data_buf_);
      if (ok) {
        server_conn_->send(&data_buf_);
        buf->retrieveAll();
      } else {
        buf->retrieveAll();
        teardown();
      }
    } else {
      buf->retrieveAll();
      abort();
    }
  }

  void onHighWaterMark(const muduo::net::TcpConnectionPtr& conn,
                       size_t bytesToSent) {
    LOG_INFO << "onHighWaterMark " << conn->name() << " bytes " << bytesToSent;
    disconnect();
  }

  static void onHighWaterMarkWeak(const boost::weak_ptr<Tunnel>& wkTunnel,
                                  const muduo::net::TcpConnectionPtr& conn,
                                  size_t bytesToSent) {
    boost::shared_ptr<Tunnel> tunnel = wkTunnel.lock();
    if (tunnel) {
      tunnel->onHighWaterMark(conn, bytesToSent);
    }
  }

 private:
  bool sent_iv_;
  ServerConnMetaPtr server_conn_meta_;
  muduo::net::TcpConnectionPtr server_conn_;
  muduo::net::TcpClient client_;
  crypto::EncryptorPtr encryptor_;

  muduo::net::Buffer data_buf_;
};

typedef boost::shared_ptr<Tunnel> TunnelPtr;

#endif
