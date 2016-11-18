
#include <map>
#include <queue>

#include <boost/bind.hpp>
#include <boost/unordered_map.hpp>

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>

#include "ikcp.h"
#include "string_utils.h"

using namespace muduo;
using namespace muduo::net;

const double kClientInterval = 1;

const int kFrameLen = sizeof(int64_t) * 2;

boost::unordered_map<std::string, IKCPCB*> kcpcbs;
boost::unordered_map<std::string, TimerId> timer_ids;
int CreateNonblockingUDPOrDie() {
  int sockfd =
      ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
  if (sockfd < 0) {
    LOG_SYSFATAL << "::socket";
  }
  return sockfd;
}

#include <muduo/base/Logging.h>
#include <muduo/base/Singleton.h>

const int kInvalidSessionId = 0;

const int kMaxSessionId = 100000;

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

    // shuffle
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
      return 0;
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

// Convenience struct for when you need a |struct sockaddr|.
struct SockaddrStorage {
  SockaddrStorage()
      : addr_len(sizeof(addr_storage)),
        addr(reinterpret_cast<struct sockaddr*>(&addr_storage)) {}
  SockaddrStorage(const SockaddrStorage& other);
  void operator=(const SockaddrStorage& other);

  struct sockaddr_storage addr_storage;
  socklen_t addr_len;
  struct sockaddr* const addr;
};

class KCPSession {
 public:
  KCPSession() : session_id_(kInvalidSessionId) {}
  ~KCPSession() {}

  struct Params {
    Params();
    ~Params();

    int nodelay;
    int interval;
    int resend;
    int nocongestion;
  };

  bool Init(int session_id, const Params& params);

  int session_id() const { return session_id_; }

  const boost::any& context() const { return context_; }
  void set_context(const boost::any& context) { context_ = context; }

 private:
  int session_id_;
  ScopedKCPSessionPtr kcp_;

  boost::any context_;
};

