
#include "kcp_session.h"

#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>

#include "common/string_utils.h"

KCPSession::KCPSession() : session_id_(kInvalidSessionId), kcp_(NULL) {}

bool KCPSession::Init(int session_id, const Params& params) {
  // int session_id =
  // KCPSessionIdInitSingleton::GetInstance().GetNextSessionId();
  // if (session_id == kInvalidSessionId) {
  // return false;
  //}
  assert(session_id_ == kInvalidSessionId);
  assert(!kcp_.get());

  IKCPCB* kcp = ikcp_create(session_id, this);
  if (kcp == NULL) {
    return false;
  }

  ScopedKCPSessionPtr scoped_kcp(new ScopedKCPSession(kcp));
  ikcp_setoutput(kcp, KCPSession::kcp_output_callback);

  int rv = ikcp_nodelay(kcp, params.nodelay, params.interval, params.resend,
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
  assert(kcp_->get() != NULL);

  int result = ikcp_input(kcp_->get(), buf, len);
  if (result == -1) {
    LOG_ERROR << "kcp header data invalid";
  } else if (result == -2) {
    LOG_ERROR << "kcp data has been truncated";
  } else if (result == -3) {
    LOG_ERROR << "kcp header cmd invalid";
  }

  LOG_INFO << "kcp_session #" << session_id_ << " feed data, len = " << len;

  while (true) {
    int data_size = ikcp_peeksize(kcp_->get());
    if (data_size < 0) {
      break;
    }

    LOG_INFO << "kcp_session #" << session_id_
             << " peek data size = " << data_size;

    input_buf_.ensureWritableBytes(data_size);
    int rn = ikcp_recv(kcp_->get(), input_buf_.beginWrite(), data_size);
    if (UNLIKELY(rn < 0)) {
      LOG_FATAL << "::ikcp_recv impossible";
    }

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
  output_callback_(shared_from_this(), &output_buf_);
}

int KCPSession::kcp_output_callback(const char* buf, int len, IKCPCB* kcp,
                                    void* user) {
  (void)kcp;

  KCPSession* kcp_session = reinterpret_cast<KCPSession*>(user);
  kcp_session->Output(buf, len);

  return 0;
}
