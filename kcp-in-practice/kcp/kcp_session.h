#ifndef KCP_SESSION_H
#define KCP_SESSION_H

#include <queue>

#include <boost/any.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include <muduo/base/Logging.h>
#include <muduo/base/Singleton.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/TimerId.h>
#include <muduo/net/InetAddress.h>

#include "common/macros.h"

#include "ikcp.h"

const uint32_t kInvalidSessionId = 0;
const uint32_t kMaxSessionId = 100000;

namespace muduo {
namespace net {

class EventLoop;
}
}

class KCPSessionIdInitSingleton : boost::noncopyable {
 public:
  static KCPSessionIdInitSingleton& GetInstance() {
    return muduo::Singleton<KCPSessionIdInitSingleton>::instance();
  }

  void InitAvailSessionIds() {
    std::vector<uint32_t> vi;
    for (uint32_t i = 1; i <= kMaxSessionId; ++i) {
      vi.push_back(i);
    }

    ::srand(::time(NULL));
    size_t len = vi.size();
    while (len > 0) {
      uint32_t i = rand() % len;
      uint32_t id = vi[i];
      vi[i] = vi[len - 1];
      vi[len - 1] = id;
      len--;
    }

    for (size_t i = 0; i < vi.size(); ++i) {
      avail_session_ids_.push(vi[i]);
    }
  }

  uint32_t GetNextSessionId() {
    if (avail_session_ids_.empty()) {
      return kInvalidSessionId;
    }

    uint32_t id = avail_session_ids_.front();
    avail_session_ids_.pop();

    return id;
  }

  void ReleaseSessionId(uint32_t session_id) {
    avail_session_ids_.push(session_id);
  }

 private:
  friend muduo::Singleton<KCPSessionIdInitSingleton>;

  KCPSessionIdInitSingleton() { InitAvailSessionIds(); }

 private:
  std::queue<uint32_t> avail_session_ids_;
};

struct PACKED MetaData {
  enum { kSyn, kAck, kPsh, kPing, kPong };
  uint8_t kind;
  uint32_t session_id;
  uint32_t key;
};

struct ScopedKCPSession {
 public:
  explicit ScopedKCPSession(IKCPCB* kcp) : kcp_(CHECK_NOTNULL(kcp)) {}

  ~ScopedKCPSession() { ikcp_release(kcp_); }

  IKCPCB* get() { return kcp_; }

 private:
  IKCPCB* const kcp_;
};

typedef boost::scoped_ptr<ScopedKCPSession> ScopedKCPSessionPtr;

class KCPSession;
typedef boost::shared_ptr<KCPSession> KCPSessionPtr;

class KCPSession : boost::noncopyable,
                   public boost::enable_shared_from_this<KCPSession> {
 public:
  typedef boost::function<void(const KCPSessionPtr&, muduo::net::Buffer*)>
      MessageCallback;
  typedef boost::function<void(const KCPSessionPtr, muduo::net::Buffer*)>
      OutputCallback;
  typedef boost::function<void(const KCPSessionPtr&)> CloseCallback;
  typedef boost::function<void(const KCPSessionPtr&)> ConnectionCallback;

  struct Params {
    int snd_wnd;
    int rcv_wnd;
    int nodelay;
    int interval;
    int resend;
    int nocongestion;
    int mtu;
    int stream_mode;
  };

  explicit KCPSession(muduo::net::EventLoop* loop);
  ~KCPSession();

  bool Init(uint32_t session_id, uint32_t key, const Params& params);

  void Feed(const char* buf, size_t len);
  void Feed(muduo::net::Buffer* buf);

  void Output(const char* buf, size_t len);

  void Send(const char* buf, size_t len);
  void Send(muduo::net::Buffer* buf);

  muduo::net::EventLoop* get_loop() const { return loop_; }

  uint32_t session_id() const { return session_id_; }
  uint32_t key() const { return key_; }

  void set_send_no_delay(bool send_no_delay) { send_no_delay_ = send_no_delay; }

  void set_fast_ack(bool fast_ack) { fast_ack_ = fast_ack; }

  void set_connection_callback(const ConnectionCallback& cb) {
    connection_callback_ = cb;
  }

  void set_message_callback(const MessageCallback& cb) {
    message_callback_ = cb;
  }

  void set_output_callback(const OutputCallback& cb) { output_callback_ = cb; }

  void set_close_callback(const CloseCallback& cb) { close_callback_ = cb; }

  const boost::any& context() const { return context_; }
  void set_context(const boost::any& context) { context_ = context; }

  muduo::net::Buffer* input_buf() { return &input_buf_; }

  muduo::net::Buffer* output_buf() { return &output_buf_; }

  const muduo::net::InetAddress& peer_address() const { return peer_address_; };
  void set_peer_address(const muduo::net::InetAddress& addr) {
    peer_address_ = addr;
  };

  uint32_t PendingDataSize() const;

  bool IsLinkAlive() const;

  void OnUpdateTimeOut();

  bool FlushImmediately() ;

  static int OnKCPOutput(const char* buf, int len, IKCPCB* kcp, void* user);

  static void OnUpdateTimeOutWeak(
      const boost::weak_ptr<KCPSession>& wk_kcp_session);

 private:
  enum EventType { kFeed, kSend };

  // diff <= 2147483647
  static int32_t TimeDiff(uint32_t l, uint32_t r) { return l - r; }

  void MaybeNeedUpdate(EventType event_type);

  bool DoUpdate(uint32_t now_in_ms, bool directly);

 private:
  muduo::net::EventLoop* const loop_;
  ScopedKCPSessionPtr kcp_;
  uint32_t session_id_;
  uint32_t key_;
  bool send_no_delay_;
  bool fast_ack_;

  ConnectionCallback connection_callback_;
  MessageCallback message_callback_;
  OutputCallback output_callback_;
  CloseCallback close_callback_;

  muduo::net::Buffer input_buf_;
  muduo::net::Buffer output_buf_;
  muduo::net::InetAddress peer_address_;

  boost::any context_;
};

const KCPSession::Params kFastModeKCPParams = {128, 128, 1, 50, 2, 1, 1400, 0};
const KCPSession::Params kNormalModeKCPParams = {32, 32, 0, 100, 0, 0, 1400, 1};

#endif
