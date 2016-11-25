
#include "kcp_session.h"

#include <muduo/base/Types.h>

#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>

KCPSession::KCPSession(muduo::net::EventLoop* loop)
    : loop_(CHECK_NOTNULL(loop)), session_id_(kInvalidSessionId), kcp_(NULL) {}

KCPSession::~KCPSession() {
  LOG_INFO << "~KCPSession #session_id: " << session_id_;
}

bool KCPSession::Init(int session_id, const Params& params) {
  assert(session_id_ == kInvalidSessionId);
  assert(!kcp_.get());

  IKCPCB* kcp = ikcp_create(session_id, this);
  if (kcp == NULL) {
    return false;
  }

  ScopedKCPSessionPtr scoped_kcp(new ScopedKCPSession(kcp));
  ikcp_setoutput(kcp, KCPSession::OnKCPOutput);

  int rv = ikcp_wndsize(kcp, params.snd_wnd, params.rcv_wnd);
  if (rv < 0) {
    return false;
  }

  rv = ikcp_nodelay(kcp, params.nodelay, params.interval, params.resend,
                    params.nocongestion);
  if (rv < 0) {
    return false;
  }

  kcp_.swap(scoped_kcp);
  session_id_ = session_id;

  return true;
}

void KCPSession::Feed(const char* buf, size_t len) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  LOG_INFO << "kcp session #" << session_id_ << " feed data, len: " << len;

  int result = ikcp_input(kcp_->get(), buf, len);
  if (result == -1) {
    LOG_ERROR << "kcp header data invalid";
  } else if (result == -2) {
    LOG_ERROR << "kcp data has been truncated";
  } else if (result == -3) {
    LOG_ERROR << "kcp header cmd invalid";
  }

  MaybeNeedUpdate();

  while (true) {
    int data_size = ikcp_peeksize(kcp_->get());
    if (data_size < 0) {
      break;
    }

    LOG_INFO << "kcp session #" << session_id_
             << " peek data size: " << data_size;

    input_buf_.ensureWritableBytes(data_size);
    int rn = ikcp_recv(kcp_->get(), input_buf_.beginWrite(), data_size);
    if (UNLIKELY(rn < 0)) {
      LOG_FATAL << "::ikcp_recv impossible";
    }

    input_buf_.hasWritten(rn);
    if (message_callback_) {
      message_callback_(shared_from_this(), &input_buf_);
    }
    input_buf_.retrieveAll();
  }
}

void KCPSession::Output(const char* buf, size_t len) {
  output_buf_.ensureWritableBytes(len + sizeof(MetaData));
  output_buf_.appendInt8(MetaData::PSH);
  output_buf_.appendInt32(session_id());
  output_buf_.append(buf, len);
  if (output_callback_) {
    output_callback_(shared_from_this(), &output_buf_);
  }
  output_buf_.retrieveAll();
}

void KCPSession::Send(const char* buf, size_t len) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get() != NULL);

  int result = ikcp_send(kcp_->get(), buf, static_cast<int>(len));
  if (result < 0) {
    LOG_ERROR << "ikcp_send data failed, len = " << len;
  }

  MaybeNeedUpdate();
}

bool KCPSession::IsLinkAlive() const {
  assert(kcp_->get());

  return (kcp_->get()->state == 0);
}

void KCPSession::MaybeNeedUpdate() {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  muduo::Timestamp now_ts = muduo::Timestamp::now();
  uint32_t now_ms = now_ts.microSecondsSinceEpoch() / 1000;
  uint32_t next_flush_ms = ikcp_check(kcp_->get(), now_ms);

  if (next_flush_ms <= now_ms) {
    bool still_alive = DoUpdate(now_ts);
    if (!still_alive) {
      return;
    }
    next_flush_ms = ikcp_check(kcp_->get(), now_ms);
  }

  bool need_register = false;
  if (timer_info_.next_flush_ms == 0) {
    need_register = true;
  } else if (next_flush_ms < timer_info_.next_flush_ms) {
    loop_->cancel(timer_info_.timer_id);
    need_register = true;
  }

  if (need_register) {
    muduo::Timestamp new_expired_ts(now_ts.microSecondsSinceEpoch() +
                                    (next_flush_ms - now_ms) * 1000);
    timer_info_.next_flush_ms = next_flush_ms;
    timer_info_.timer_id = loop_->runAt(
        new_expired_ts,
        boost::bind(&KCPSession::OnUpdateTimeOutWeak,
                    boost::weak_ptr<KCPSession>(shared_from_this())));
  }
}

void KCPSession::OnUpdateTimeOut() {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  muduo::Timestamp now_ts = muduo::Timestamp::now();
  bool still_alive = DoUpdate(now_ts);
  if (!still_alive) {
    return;
  }

  uint32_t now_ms = now_ts.microSecondsSinceEpoch() / 1000;
  uint32_t next_flush_ms = ikcp_check(kcp_->get(), now_ms);
  muduo::Timestamp new_expired_ts(now_ts.microSecondsSinceEpoch() +
                                  (next_flush_ms - now_ms) * 1000);
  loop_->runAt(new_expired_ts,
               boost::bind(&KCPSession::OnUpdateTimeOutWeak,
                           boost::weak_ptr<KCPSession>(shared_from_this())));
}

bool KCPSession::DoUpdate(muduo::Timestamp now) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get() != NULL);

  uint32_t now_ms = now.microSecondsSinceEpoch() / 1000;
  ikcp_update(kcp_->get(), now_ms);

  if (!IsLinkAlive()) {
    LOG_INFO << "kcp session #" << session_id_ << " not alive";
    if (close_callback_) {
      close_callback_(shared_from_this());
    }
    return false;
  }

  return true;
}

int KCPSession::OnKCPOutput(const char* buf, int len, IKCPCB* kcp, void* user) {
  (void)kcp;

  KCPSession* kcp_session = reinterpret_cast<KCPSession*>(user);
  kcp_session->Output(buf, len);

  return 0;
}

void KCPSession::OnUpdateTimeOutWeak(
    const boost::weak_ptr<KCPSession>& wk_kcp_session) {
  KCPSessionPtr kcp_session = wk_kcp_session.lock();
  if (kcp_session) {
    kcp_session->OnUpdateTimeOut();
  }
}
