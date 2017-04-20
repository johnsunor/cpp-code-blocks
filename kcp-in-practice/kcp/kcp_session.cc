
#include "kcp_session.h"

#include <muduo/base/Types.h>
#include <muduo/net/EventLoop.h>

KCPSession::KCPSession(muduo::net::EventLoop* loop)
    : loop_(CHECK_NOTNULL(loop)),
      kcp_(NULL),
      session_id_(kInvalidSessionId),
      key_(0),
      send_no_delay_(false),
      fast_ack_(false) {}

KCPSession::~KCPSession() {
  LOG_INFO << "~KCPSession #session_id: " << session_id_;
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

  kcp->stream = params.stream_mode;

  int rv = ikcp_wndsize(kcp, params.snd_wnd, params.rcv_wnd);
  if (rv < 0) {
    return false;
  }

  rv = ikcp_setmtu(kcp, params.mtu);
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
}

void KCPSession::Send(const char* buf, size_t len) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get() != NULL);

  int result = ikcp_send(kcp_->get(), buf, len);
  if (result < 0) {
    LOG_ERROR << "ikcp_send data failed, len = " << len;
  }

  MaybeNeedUpdate(kSend);
}

void KCPSession::Send(muduo::net::Buffer* buf) {
  Send(buf->peek(), buf->readableBytes());
  buf->retrieveAll();
}

uint32_t KCPSession::PendingDataSize() const {
  assert(kcp_->get());
  return ikcp_waitsnd(kcp_->get());
}

bool KCPSession::IsLinkAlive() const {
  assert(kcp_->get());

  return (kcp_->get()->state == 0);
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

  while (true) {
    int data_size = ikcp_peeksize(kcp_->get());
    if (data_size <= 0) {
      break;
    }

    LOG_DEBUG << "kcp session #" << session_id_
              << " peek data size: " << data_size;

    input_buf_.ensureWritableBytes(data_size);
    int rn = ikcp_recv(kcp_->get(), input_buf_.beginWrite(), data_size);
    if (UNLIKELY(rn <= 0)) {
      LOG_FATAL << "impossible: rn <= 0";
    }
    input_buf_.hasWritten(rn);

    message_callback_(shared_from_this(), &input_buf_);
  }

  MaybeNeedUpdate(kFeed);
}

void KCPSession::MaybeNeedUpdate(EventType event_type) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  if (UNLIKELY(ikcp_updated(kcp_->get()) == 0)) {
    muduo::Timestamp now_ts = muduo::Timestamp::now();
    uint32_t now_in_ms = (now_ts.microSecondsSinceEpoch() / 1000) & 0xFFFFFFFFu;
    if (!DoUpdate(now_in_ms, false)) {
      return;
    }

    uint32_t next_ts_flush = ikcp_ts_flush(kcp_->get(), now_in_ms);
    uint32_t diff = TimeDiff(next_ts_flush, now_in_ms) * 1000;

    muduo::Timestamp new_expired_ts(now_ts.microSecondsSinceEpoch() + diff);
    loop_->runAt(new_expired_ts,
                 boost::bind(&KCPSession::OnUpdateTimeOutWeak,
                             boost::weak_ptr<KCPSession>(shared_from_this())));
  } else if (event_type == kFeed) {
    uint32_t npending_ack = ikcp_npending_ack(kcp_->get());
    if (npending_ack > 0) {
      if (fast_ack_ || npending_ack >= 2) {
        ikcp_flush_ack(kcp_->get());
      }

#ifdef KCP_EXPERIMENT
      muduo::Timestamp now_ts = muduo::Timestamp::now();
      uint32_t now_in_ms =
          (now_ts.microSecondsSinceEpoch() / 1000) & 0xFFFFFFFFu;
      if (ikcp_can_snd_new_seg(kcp_->get())) {
        ikcp_flush_snd_queue(kcp_->get(), now_in_ms);
      }

      if (ikcp_need_fast_resent(kcp_->get())) {
        ikcp_flush_fast_resent_queue(kcp_->get(), now_in_ms);
      }
#endif
    }
    if (ikcp_need_ask_tell(kcp_->get())) {
      ikcp_flush_wnd(kcp_->get());
    }
  } else if (event_type == kSend) {
    if (send_no_delay_ && ikcp_can_snd_new_seg(kcp_->get())) {
      muduo::Timestamp now_ts = muduo::Timestamp::now();
      uint32_t now_in_ms =
          (now_ts.microSecondsSinceEpoch() / 1000) & 0xFFFFFFFFu;
      ikcp_flush_snd_queue(kcp_->get(), now_in_ms);
    }
  }
}

bool KCPSession::FlushImmediately() {
  LOG_INFO << "PendingDataSize: " << PendingDataSize();

  muduo::Timestamp now_ts = muduo::Timestamp::now();
  uint32_t now_in_ms = (now_ts.microSecondsSinceEpoch() / 1000) & 0xFFFFFFFFu;
  return DoUpdate(now_in_ms, true);
}

void KCPSession::OnUpdateTimeOut() {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get());

  muduo::Timestamp now_ts = muduo::Timestamp::now();
  uint32_t now_in_ms = (now_ts.microSecondsSinceEpoch() / 1000) & 0xFFFFFFFFu;
  bool still_alive = DoUpdate(now_in_ms, false);
  if (!still_alive) {
    return;
  }

  uint32_t next_ts_flush = ikcp_ts_flush(kcp_->get(), now_in_ms);
  int32_t diff = TimeDiff(next_ts_flush, now_in_ms) * 1000;

  muduo::Timestamp new_expired_ts(now_ts.microSecondsSinceEpoch() + diff);
  loop_->runAt(new_expired_ts,
               boost::bind(&KCPSession::OnUpdateTimeOutWeak,
                           boost::weak_ptr<KCPSession>(shared_from_this())));
}

bool KCPSession::DoUpdate(uint32_t now_in_ms, bool directly) {
  assert(session_id_ != kInvalidSessionId);
  assert(kcp_->get() != NULL);

  ikcp_update(kcp_->get(), now_in_ms, directly);

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
