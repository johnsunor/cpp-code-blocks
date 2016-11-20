#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/resource.h>
#include <muduo/base/ThreadLocalSingleton.h>
#include <muduo/base/Mutex.h>

#include <boost/ptr_container/ptr_vector.hpp>

#include <gflags/gflags.h>

#include "resolver.h"
#include "encryptor_openssl.h"
#include "tunnel.h"
#include "utils/string_utils.h"

DEFINE_string(cipher_name, "aes-256-cfb", "cipher name for you wanted to use");
DEFINE_string(passwd, "aes256cfb", "cipher_passwd for you wanted to use");
DEFINE_int32(port, 3389, "server port for you wanted to use");
DEFINE_int32(thread_num, 1, "server thread num for you wanted to use");

class TCPRelayServer : boost::noncopyable {
 public:
  typedef std::map<muduo::string, ServerConnMetaPtr> MetaList;
  typedef std::map<muduo::string, TunnelPtr> TunnelList;
  typedef boost::shared_ptr<cdns::Resolver> ResolverPtr;

  TCPRelayServer(muduo::net::EventLoop* loop,
                 const muduo::net::InetAddress& listenAddr)
      : server_(loop, listenAddr, "TCPRelayServer") {
    server_.setConnectionCallback(
        boost::bind(&TCPRelayServer::onServerConnection, this, _1));
    server_.setMessageCallback(
        boost::bind(&TCPRelayServer::onServerMessage, this, _1, _2, _3));
  }

  void setThreadNum(int numThreads) { server_.setThreadNum(numThreads); }

  void start() {
    server_.setThreadInitCallback(
        boost::bind(&TCPRelayServer::threadInit, this, _1));
    server_.start();
  }

  void resolveCallback(const ServerConnMetaPtr& meta,
                       const muduo::net::InetAddress& addr) {
    const crypto::EncryptorPtr& decryptor = meta->decryptor();
    crypto::EncryptorPtr encryptor(new crypto::Encryptor);
    assert(encryptor->Init(decryptor->cipher_name(), decryptor->passwd(),
                           decryptor->iv(), true));  // mix iv

    const muduo::net::TcpConnectionPtr& conn = meta->conn();
    muduo::net::InetAddress server_addr(addr.toIp(), meta->dst_port());
    TunnelPtr tunnel(new Tunnel(conn->getLoop(), server_addr, meta, encryptor));
    tunnel->setup();
    tunnel->connect();
    LocalTunnelList::instance()[conn->name()] = tunnel;
    LOG_INFO << "connecting to " << meta->dst_hostname() << ":"
             << meta->dst_port() << "(" << server_addr.toIpPort() << ")";
  }

 private:
  void onServerConnection(const muduo::net::TcpConnectionPtr& conn) {
    LOG_DEBUG << conn->name() << (conn->connected() ? " UP" : " DOWN");
    if (conn->connected()) {
      conn->setTcpNoDelay(true);
    } else {
      MetaList::iterator it1 = LocalMetaList::instance().find(conn->name());
      if (it1 != LocalMetaList::instance().end()) {
        LocalMetaList::instance().erase(it1);
        TunnelList::iterator it2 =
            LocalTunnelList::instance().find(conn->name());
        if (it2 != LocalTunnelList::instance().end()) {
          it2->second->disconnect();
          LocalTunnelList::instance().erase(it2);
        }
      }
    }
  }

  void onServerMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buf, muduo::Timestamp) {
    std::map<muduo::string, ServerConnMetaPtr>::iterator it =
        LocalMetaList::instance().find(conn->name());

    if (it == LocalMetaList::instance().end()) {
      size_t cipher_iv_len = crypto::GetCipherIvLength(FLAGS_cipher_name);
      if (buf->readableBytes() < cipher_iv_len) {
        return;
      }

      std::string iv(buf->peek(), cipher_iv_len);

      crypto::EncryptorPtr decryptor(new crypto::Encryptor);
      assert(decryptor->Init(FLAGS_cipher_name, FLAGS_passwd, iv, false));

      ServerConnMetaPtr meta(new ServerConnMeta(conn, decryptor));
      if (!meta->Decrypt(
              buf->peek() + cipher_iv_len,
              static_cast<int>(buf->readableBytes() - cipher_iv_len))) {
        conn->shutdown();
        return;
      }

      if (meta->ParseRequest()) {
        // async dns resolve
        if (!LocalResolver::instance()->resolve(
                meta->dst_hostname(),
                boost::bind(&TCPRelayServer::resolveCallback, this, meta,
                            _1))) {
          conn->shutdown();
          return;
        }

        buf->retrieveAll();
        LocalMetaList::instance()[conn->name()] = meta;
      }
    } else {
      ServerConnMetaPtr& meta = it->second;
      if (!meta->Decrypt(buf)) {
        conn->shutdown();
        return;
      }

      buf->retrieveAll();
      if (!conn->getContext().empty()) {
        const muduo::net::TcpConnectionPtr& clientConn =
            boost::any_cast<const muduo::net::TcpConnectionPtr&>(
                conn->getContext());
        clientConn->send(meta->mutable_buf());
      }
    }
  }

  void threadInit(muduo::net::EventLoop* loop) {
    assert(LocalMetaList::pointer() == NULL);
    assert(LocalResolver::pointer() == NULL);

    LocalMetaList::instance();
    LocalResolver::instance();

    assert(LocalMetaList::pointer() != NULL);
    assert(LocalResolver::pointer() != NULL);
    LocalResolver::instance().reset(
        new cdns::Resolver(loop, cdns::Resolver::kDNSandHostsFile));
  }

  typedef muduo::ThreadLocalSingleton<MetaList> LocalMetaList;
  typedef muduo::ThreadLocalSingleton<ResolverPtr> LocalResolver;
  typedef muduo::ThreadLocalSingleton<TunnelList> LocalTunnelList;

  muduo::net::TcpServer server_;
};

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  muduo::g_logLevel = muduo::Logger::INFO;

  LOG_INFO << "FLAGS_port: " << FLAGS_port
           << ", FLAGS_thread_num: " << FLAGS_thread_num
           << ", FLAGS_cipher_name: " << FLAGS_cipher_name
           << ", FLAGS_passwd: " << FLAGS_passwd;

  assert(crypto::SupportedCipher(FLAGS_cipher_name));

  muduo::net::InetAddress listenAddr(static_cast<uint16_t>(FLAGS_port));

  muduo::net::EventLoop loop;
  TCPRelayServer server(&loop, listenAddr);
  server.setThreadNum(FLAGS_thread_num);

  server.start();
  loop.loop();

  return 0;
}
