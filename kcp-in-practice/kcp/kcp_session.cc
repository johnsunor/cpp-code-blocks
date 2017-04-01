
#include "kcp_session.h"

#include <muduo/base/Types.h>
#include <muduo/net/EventLoop.h>

KCPSession::KCPSession(muduo::net::EventLoop* loop)
    : loop_(CHECK_NOTNULL(loop)),
      kcp_(NULL),
      session_id_(kInvalidSessionId),
      key_(0),
      send_no_delay_(false) {}

KCPSession::~KCPSession() {
  LOG_DEBUG << "~KCPSession #session_id: " << session_id_;
}

bool KCPSession::Init(uint32_t session_id, uint32_t key, const Params& params) {
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
  key_ = key;

  return true;
}

void KCPSession::Feed(const char* buf, size_t len) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  LOG_DEBUG << "kcp session #" << session_id_ << " feed data len: " << len;

  int result = ikcp_input(kcp_->get(), buf, len);
  if (result == -1) {
    LOG_ERROR << "kcp header data invalid";
  } else if (result == -2) {
    LOG_ERROR << "kcp data has been truncated";
  } else if (result == -3) {
    LOG_ERROR << "kcp header cmd invalid";
  }
  MaybeNeedUpdate(false);

  while (true) {
    int data_size = ikcp_peeksize(kcp_->get());
    if (data_size < 0) {
      break;
    }

    LOG_DEBUG << "kcp session #" << session_id_
              << " peek data size: " << data_size;

    input_buf_.ensureWritableBytes(data_size);
    int rn = ikcp_recv(kcp_->get(), input_buf_.beginWrite(), data_size);
    if (UNLIKELY(rn < 0)) {
      LOG_FATAL << "impossible: rn < 0";
    }
    input_buf_.hasWritten(rn);

    if (message_callback_) {
      message_callback_(shared_from_this(), &input_buf_);
    }
    input_buf_.retrieveAll();
  }
}

void KCPSession::Feed(muduo::net::Buffer* buf) {
  Feed(buf->peek(), buf->readableBytes());
  buf->retrieveAll();
}

void KCPSession::Output(const char* buf, size_t len) {
  output_buf_.ensureWritableBytes(sizeof(MetaData) + len);
  output_buf_.appendInt8(MetaData::kPsh);
  output_buf_.appendInt32(session_id_);
  output_buf_.appendInt32(key_);
  output_buf_.append(buf, len);
  output_callback_(shared_from_this(), &output_buf_);
  output_buf_.retrieveAll();
  // output_callback_(shared_from_this(), buf, len);
}

void KCPSession::FlushNoDelay(uint32_t now_ms) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get() != NULL);

  kcp_->get()->current = now_ms;
  ikcp_flush(kcp_->get());

  if (!IsLinkAlive()) {
    LOG_DEBUG << "kcp session #" << session_id_ << " not alive";
    if (close_callback_) {
      close_callback_(shared_from_this());
    }
  }
}

void KCPSession::Send(const char* buf, size_t len) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get() != NULL);

  // not in stream mode
  int result = ikcp_send(kcp_->get(), buf, len);
  if (result < 0) {
    LOG_ERROR << "ikcp_send data failed, len = " << len;
  }

  MaybeNeedUpdate(send_no_delay_);
}

void KCPSession::Send(muduo::net::Buffer* buf) {
  Send(buf->peek(), buf->readableBytes());
  buf->retrieveAll();
}

bool KCPSession::IsLinkAlive() const {
  assert(kcp_->get());

  return (kcp_->get()->state == 0);
}

uint32_t KCPSession::UpdateTsFlush(uint32_t now_ms) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  uint32_t new_ts_flush = ikcp_check(kcp_->get(), now_ms);
  if (new_ts_flush < kcp_->get()->ts_flush) {
    kcp_->get()->ts_flush = new_ts_flush;
  }

  return kcp_->get()->ts_flush;
}

void KCPSession::MaybeNeedUpdate(bool no_delay) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  muduo::Timestamp now_ts = muduo::Timestamp::now();
  uint32_t now_ms = now_ts.microSecondsSinceEpoch() / 1000;
  // uint32_t next_flush_ms = ikcp_check(kcp_->get(), now_ms);
  uint32_t next_flush_ms = UpdateTsFlush(now_ms);

  if (next_flush_ms <= now_ms) {
    bool still_alive = DoUpdate(now_ms);
    if (!still_alive) {
      return;
    }
    // next_flush_ms = ikcp_check(kcp_->get(), now_ms);
    next_flush_ms = UpdateTsFlush(now_ms);
  } else if (no_delay) {
    FlushNoDelay(now_ms);
  }

  bool need_register = false;
  if (flush_timer_.next_flush_ms == 0) {
    need_register = true;
  } else if (next_flush_ms < flush_timer_.next_flush_ms) {
    loop_->cancel(flush_timer_.timer_id);
    need_register = true;
  }

  if (need_register) {
    muduo::Timestamp new_expired_ts(now_ts.microSecondsSinceEpoch() +
                                    (next_flush_ms - now_ms) * 1000);
    flush_timer_.next_flush_ms = next_flush_ms;
    flush_timer_.timer_id = loop_->runAt(
        new_expired_ts,
        boost::bind(&KCPSession::OnUpdateTimeOutWeak,
                    boost::weak_ptr<KCPSession>(shared_from_this())));
  }
}

void KCPSession::OnUpdateTimeOut() {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  muduo::Timestamp now_ts = muduo::Timestamp::now();
  uint32_t now_ms = now_ts.microSecondsSinceEpoch() / 1000;
  bool still_alive = DoUpdate(now_ms);
  if (!still_alive) {
    return;
  }

  // uint32_t next_flush_ms = ikcp_check(kcp_->get(), now_ms);
  uint32_t next_flush_ms = UpdateTsFlush(now_ms);
  muduo::Timestamp new_expired_ts(now_ts.microSecondsSinceEpoch() +
                                  (next_flush_ms - now_ms) * 1000);
  flush_timer_.next_flush_ms = next_flush_ms;
  flush_timer_.timer_id = loop_->runAt(
      new_expired_ts,
      boost::bind(&KCPSession::OnUpdateTimeOutWeak,
                  boost::weak_ptr<KCPSession>(shared_from_this())));
}

bool KCPSession::DoUpdate(uint32_t now_ms) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get() != NULL);

  ikcp_update(kcp_->get(), now_ms);

  if (!IsLinkAlive()) {
    LOG_DEBUG << "kcp session #" << session_id_ << " not alive";
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
