
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

#include "common/macros.h"

#include "ikcp.h"

const int kInvalidSessionId = 0;
const int kMaxSessionId = 100000;

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
    std::vector<int> vi;
    for (int i = 1; i <= kMaxSessionId; ++i) {
      vi.push_back(i);
    }

    ::srand(::time(NULL));
    size_t len = vi.size();
    while (len > 0) {
      int i = rand() % len;
      int id = vi[i];
      vi[i] = vi[len - 1];
      vi[len - 1] = id;
      len--;
    }

    for (size_t i = 0; i < vi.size(); ++i) {
      avail_session_ids_.push(vi[i]);
    }
  }

  int GetNextSessionId() {
    if (avail_session_ids_.empty()) {
      return kInvalidSessionId;
    }

    int id = avail_session_ids_.front();
    avail_session_ids_.pop();

    return id;
  }

  void ReleaseSessionId(int session_id) { avail_session_ids_.push(session_id); }

 private:
  friend muduo::Singleton<KCPSessionIdInitSingleton>;

  KCPSessionIdInitSingleton() { InitAvailSessionIds(); }

 private:
  std::queue<int> avail_session_ids_;
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

  typedef boost::function<void(const KCPSessionPtr&, muduo::net::Buffer*)>
      OutputCallback;

  typedef boost::function<void(const KCPSessionPtr&)> CloseCallback;

  struct PACKED MetaData {
    enum { SYN, ACK, PSH, PING };
    uint8_t kind;
    int session_id;
  };

  struct Params {
    int snd_wnd;
    int rcv_wnd;
    int nodelay;
    int interval;
    int resend;
    int nocongestion;
  };

  explicit KCPSession(muduo::net::EventLoop* loop);
  ~KCPSession();

  bool Init(int session_id, const Params& params);

  void Feed(const char* buf, size_t len);

  void Output(const char* buf, size_t len);

  void Send(const char* buf, size_t len);

  muduo::net::EventLoop* get_loop() const { return loop_; }

  int session_id() const { return session_id_; }

  void set_message_callback(const MessageCallback& cb) {
    message_callback_ = cb;
  }

  void set_output_callback(const OutputCallback& cb) { output_callback_ = cb; }
  void set_close_callback(const CloseCallback& cb) { close_callback_ = cb; }

  const boost::any& context() const { return context_; }
  void set_context(const boost::any& context) { context_ = context; }

  bool IsLinkAlive() const;

  void MaybeNeedUpdate();
  void OnUpdateTimeOut();

  static int OnKCPOutput(const char* buf, int len, IKCPCB* kcp, void* user);

  static void OnUpdateTimeOutWeak(
      const boost::weak_ptr<KCPSession>& wk_kcp_session);

 private:
  bool DoUpdate(muduo::Timestamp now);

  struct TimerInfo {
    TimerInfo() : next_flush_ms(0) {}
    uint32_t next_flush_ms;
    muduo::net::TimerId timer_id;
  };

 private:
  muduo::net::EventLoop* const loop_;
  int session_id_;
  ScopedKCPSessionPtr kcp_;

  MessageCallback message_callback_;
  OutputCallback output_callback_;
  CloseCallback close_callback_;

  muduo::net::Buffer input_buf_;
  muduo::net::Buffer output_buf_;

  boost::any context_;

  TimerInfo timer_info_;
};

const KCPSession::Params kFastModeKCPParams = {128, 128, 1, 10, 2, 1};
const KCPSession::Params kNormalModeKCPParams = {32, 32, 0, 100, 0, 0};
