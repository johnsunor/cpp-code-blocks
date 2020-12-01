
#include "kcp_server.h"

#include <linux/errqueue.h>
#include <sys/socket.h>

#include <functional>
#include <memory>

#include <zconf.h>
#include <zlib.h>

#include <muduo/base/Logging.h>
#include <muduo/base/ThreadLocalSingleton.h>
#include <muduo/base/ThreadPool.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/InetAddress.h>

#include "common/macros.h"

#include "kcp_callbacks.h"
#include "kcp_constants.h"
#include "kcp_packets.h"
#include "kcp_session.h"
#include "udp_socket.h"
#include "urandom.h"

KCPServer::KCPServer(muduo::net::EventLoop* loop) : loop_(CHECK_NOTNULL(loop)) {
  Initialize();
}

KCPServer::~KCPServer() {
  loop_->assertInLoopThread();

  channel_->disableAll();
  channel_->remove();

  loop_->cancel(periodic_task_timer_);

  int num_sessions = 0;
  std::atomic<int> num_closed_sessions{0};
  for (auto& s : session_map_) {
    KCPSessionPtr& session = s.second;
    session->loop()->runInLoop([session, &num_closed_sessions] {
      session->Close(true);
      ++num_closed_sessions;
    });
    ++num_sessions;
  }

  // spin
  int try_times = 0;
  while (num_closed_sessions < num_sessions &&
         ++try_times < kServerMaxWaitSessionClosedTryTimes)
    ;

  int num_unclosed_sessions = num_sessions - num_closed_sessions;
  if (num_unclosed_sessions > 0) {
    LOG_ERROR << "~KCPServer there are still have " << num_unclosed_sessions
              << " sessions not closed yet";
  }

  // ...
  // ~thread_pool_ => quit thread loop, not 100% safe
  // ...
  // ...
  // ~socket_ => close socket
}

int KCPServer::Listen(const muduo::net::InetAddress& address) {
  loop_->assertInLoopThread();

  auto socket = std::make_unique<UDPSocket>();

  socket->AllowReuseAddress();
  socket->AllowReusePort();
  socket->AllowReceiveError();

  int rc = socket->Bind(address);
  if (rc < 0) {
    LOG_ERROR << "Bind error: " << rc;
    return rc;
  }

  rc = socket->SetReceiveBufferSize(static_cast<int32_t>(kSocketReceiveBuffer));
  if (rc < 0) {
    LOG_ERROR << "SetReceiveBufferSize error: " << rc;
    return rc;
  }

  rc = socket->SetSendBufferSize(static_cast<int32_t>(kSocketSendBuffer));
  if (rc < 0) {
    LOG_ERROR << "SetSendBufferSize error: " << rc;
    return rc;
  }

  rc = socket->GetLocalAddress(&server_address_);
  if (rc < 0) {
    LOG_ERROR << "GetLocalAddress error: " << rc;
    return rc;
  }

  socket_ = std::move(socket);

  thread_pool_ =
      std::make_unique<muduo::net::EventLoopThreadPool>(loop_, "KCPServer");
  thread_pool_->setThreadNum(num_threads_);
  thread_pool_->start(
      [this](muduo::net::EventLoop* loop) { InitializeThread(loop); });

  channel_ = std::make_unique<muduo::net::Channel>(loop_, socket_->sockfd());
  channel_->setReadCallback(
      [this](muduo::Timestamp receive_time) { HandleRead(receive_time); });
  channel_->setWriteCallback([this] { HandleWrite(); });
  channel_->setErrorCallback([this] { HandleError(); });
  channel_->enableReading();

  periodic_task_timer_ = loop_->runEvery(kServerRunPeriodicTaskInterval,
                                         [this] { RunPeriodicTask(); });

  return 0;
}

void KCPServer::RunPeriodicTask() {
  muduo::Timestamp now = muduo::Timestamp::now();
  for (auto it = pending_session_map_.begin();
       it != pending_session_map_.end();) {
    std::unique_ptr<KCPPendingSession>& pending_session = it->second;
    if (pending_session->retry_times >= kServerMaxSynRetryTimes) {
      // send rst packet
      // SendRstPacket(pending_session->peer_address);
      LOG_ERROR << "session syn retry reach the limit, session_id: "
                << pending_session->session_id << ", client_address: "
                << pending_session->peer_address.toIpPort();
      it = pending_session_map_.erase(it);
      continue;
    }

    if (muduo::timeDifference(now, pending_session->syn_sent_time) >=
        kServerSynSentTimeout) {
      // send syn packet
      ++pending_session->retry_times;
      pending_session->syn_sent_time = now;
      SendPacket(SYN_PACKET, pending_session->session_id,
                 pending_session->peer_address);
      LOG_WARN << "session syn timeout the " << pending_session->retry_times
               << "th time retry syn has sent, session_id: "
               << pending_session->session_id;
    }
    ++it;
  }

  for (auto it = session_map_.begin(); it != session_map_.end();) {
    uint32_t session_id = it->first;
    KCPSessionPtr& session = it->second;

    auto idle_session_it = idle_session_map_.find(session_id);
    if (idle_session_it == idle_session_map_.end()) {
      session->loop()->runInLoop([session] { session->Close(); });
      it = session_map_.erase(it);
      time_wait_session_map_.insert(std::make_pair(session_id, now));
      LOG_ERROR << "session exists but idle session not exists, session_id: "
                << session_id;
      continue;
    }

    muduo::Timestamp last_received_time = idle_session_it->second;
    if (muduo::timeDifference(now, last_received_time) >=
        kServerSessionIdleSeconds) {
      session->loop()->runInLoop([session] { session->Close(); });
      it = session_map_.erase(it);
      idle_session_map_.erase(idle_session_it);
      time_wait_session_map_.insert(std::make_pair(session_id, now));
      LOG_INFO << "session exipred, session_id: " << session_id
               << ", last_received_time: "
               << last_received_time.toFormattedString();
      continue;
    }

    if (session->IsClosed()) {
      it = session_map_.erase(it);
      idle_session_map_.erase(idle_session_it);
      time_wait_session_map_.insert(std::make_pair(session_id, now));
      continue;
    }

    ++it;
  }

  for (auto it = time_wait_session_map_.begin();
       it != time_wait_session_map_.end();) {
    muduo::Timestamp start_time = it->second;
    if (muduo::timeDifference(now, start_time) >=
        kServerSessionTimeWaitSeconds) {
      it = time_wait_session_map_.erase(it);
      continue;
    }

    ++it;
  }
}

void KCPServer::ListenOrDie(const muduo::net::InetAddress& address) {
  int rc = Listen(address);
  if (rc != 0) {
    LOG_FATAL << "KCPServer::ListenOrDie";
  }
  LOG_INFO << "kcp server listening on " << address.toIpPort();
}

void KCPServer::Initialize() {
  mmsg_hdrs_ = std::make_unique<mmsghdr[]>(kNumPacketsPerRead);
  raw_packets_ = std::make_unique<RawPacket[]>(kNumPacketsPerRead);
  memset(mmsg_hdrs_.get(), 0, kNumPacketsPerRead * sizeof(mmsghdr));

  for (int i = 0; i < kNumPacketsPerRead; ++i) {
    RawPacket* pkt = &raw_packets_[i];
    pkt->iov.iov_base = pkt->buf;
    pkt->iov.iov_len = sizeof(pkt->buf);
    memset(&pkt->addr, 0, sizeof(pkt->addr));
    memset(pkt->buf, 0, sizeof(pkt->buf));

    struct msghdr* hdr = &mmsg_hdrs_[i].msg_hdr;
    hdr->msg_name = &pkt->addr;
    hdr->msg_namelen = sizeof(sockaddr_storage);
    hdr->msg_iov = &pkt->iov;
    hdr->msg_iovlen = 1;
    hdr->msg_control = nullptr;
    hdr->msg_controllen = 0;
    hdr->msg_flags = 0;
  }
}

void KCPServer::HandleRead(muduo::Timestamp) {
  // HandleError();

  for (int i = 0; i < kNumPacketsPerRead; ++i) {
    msghdr* hdr = &mmsg_hdrs_[i].msg_hdr;
    hdr->msg_namelen = sizeof(sockaddr_storage);
    hdr->msg_controllen = 0;
    hdr->msg_flags = 0;
  }

  muduo::net::InetAddress client_address;
  while (true) {
    int packets_read = socket_->RecvMmsg(mmsg_hdrs_.get(), kNumPacketsPerRead);
    if (packets_read < 0) {
      int saved_errno = -packets_read;
      if (!IS_EAGAIN(saved_errno)) {
        LOG_ERROR << "RecvMmsg failed with error: " << saved_errno
                  << ", detail: " << muduo::strerror_tl(saved_errno);
      }
      break;
    }

    for (int i = 0; i < packets_read; ++i) {
      if (mmsg_hdrs_[i].msg_len == 0) {
        continue;
      }

      // MSG_TRUNC
      if (mmsg_hdrs_[i].msg_len > kMaxPacketSize) {
        LOG_ERROR << "RecvMsg normal data was truncated";
        continue;
      }

      msghdr* hdr = &mmsg_hdrs_[i].msg_hdr;
      if (!SockaddrStorage::ToInetAddr(raw_packets_[i].addr, hdr->msg_namelen,
                                       &client_address)) {
        LOG_ERROR << "ToInetAddr failed with msg_namelen: " << hdr->msg_namelen;
        continue;
      }

      KCPReceivedPacket packet(static_cast<char*>(raw_packets_[i].iov.iov_base),
                               mmsg_hdrs_[i].msg_len);
      ProcessPacket(packet, client_address);
    }

    if (packets_read != kNumPacketsPerRead) {
      break;
    }
  }
}

void KCPServer::HandleWrite() {
  // SetWritable();
  //
  // for (auto& packet: queued_packets_)
  // }
  //
  // if (queued_packets_.empty()) {
  //   channel_->disableWriting();
  // }
}

void KCPServer::HandleError() {
  // man 7 udp
  // When the IP_RECVERR option is enabled, all errors are stored in the socket
  // error queue, and can be received by recvmsg(2) with the MSG_ERRQUEUE flag
  // set.
  RawPacket packet;
  packet.iov.iov_base = packet.buf;
  packet.iov.iov_len = sizeof(packet.buf);

  struct msghdr msg;
  msg.msg_iov = &packet.iov;
  msg.msg_iovlen = 1;
  msg.msg_name = &packet.addr;
  msg.msg_namelen = sizeof(packet.addr);

  char buf[kMaxAncillaryDataLength];
  msg.msg_control = buf;
  msg.msg_controllen = sizeof(buf);
  msg.msg_flags = 0;

  while (true) {
    assert(packet.iov.iov_len == sizeof(packet.buf));
    assert(msg.msg_iovlen == 1);

    msg.msg_namelen = sizeof(packet.addr);
    msg.msg_controllen = sizeof(buf);
    msg.msg_flags = 0;

    int rc = socket_->RecvMsg(&msg, MSG_ERRQUEUE);
    if (rc < 0) {
      int saved_errno = -rc;
      if (!IS_EAGAIN(saved_errno)) {
        LOG_ERROR << "RecvMsg failed with error: " << saved_errno
                  << ", detail: " << muduo::strerror_tl(saved_errno);
      }
      return;
    }

    if (msg.msg_flags & MSG_CTRUNC) {
      LOG_ERROR << "RecvMsg control data was truncated";
      continue;
    }

    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != nullptr && cmsg->cmsg_len != 0;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      if ((cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVERR) ||
          (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVERR)) {
        const struct sock_extended_err* serr =
            reinterpret_cast<const struct sock_extended_err*>(CMSG_DATA(cmsg));
        if (serr->ee_origin == SO_EE_ORIGIN_ICMP ||
            serr->ee_origin == SO_EE_ORIGIN_ICMP6) {
          muduo::net::InetAddress dest_address;
          SockaddrStorage::ToInetAddr(packet.addr, msg.msg_namelen,
                                      &dest_address);

          KCPPublicHeader public_header;
          if (public_header.ReadFrom(packet.buf, rc)) {
            auto session_it = session_map_.find(public_header.session_id);
            if (session_it != session_map_.end()) {
              KCPSessionPtr& session = session_it->second;
              PendingError pending_error = {.type = serr->ee_type,
                                            .code = serr->ee_code};
              session->loop()->runInLoop([session, pending_error] {
                session->set_pending_error(pending_error);
              });
            }
          }

          // unreachable
          // type: 3(net(0)/host(1)/proto(2)/port(3) unreachable)
          // redirection
          // type: 5(net(0)/host(1) redirection)
          // TODO: update route
          LOG_ERROR << "received icmp error: " << serr->ee_errno
                    << ", type: " << serr->ee_type
                    << ", code: " << serr->ee_code
                    << ", orignal: " << serr->ee_origin
                    << ", detail: " << muduo::strerror_tl(serr->ee_errno)
                    << ", orignal packet info: "
                    << "{ length: " << rc
                    << ", public header: " << public_header
                    << ", dest address: " << dest_address.toIpPort() << " }";
        }
      }
    }
  }
}

bool KCPServer::GenerateSessionId(uint32_t* session_id) const {
  uint32_t rand_id = 0;

  for (int i = 0; i < kServerMaxGenSessionIdTryTimes; ++i) {
    if (!URandom::GetInstance().RandBytes(&rand_id, sizeof(uint32_t))) {
      continue;
    }

    if (session_map_.count(rand_id) > 0) {
      continue;
    }

    if (time_wait_session_map_.count(rand_id) > 0) {
      continue;
    }

    *session_id = rand_id;
    return true;
  }

  return false;
}

void KCPServer::SendPacket(uint8_t packet_type, uint32_t session_id,
                           const muduo::net::InetAddress& client_address) {
  KCPPublicHeader public_header;
  public_header.packet_type = packet_type;
  public_header.session_id = session_id;

  char buf[kMaxPacketSize];
  if (!public_header.WriteTo(buf, sizeof(buf))) {
    LOG_ERROR << "WriteTo failed, session_id: " << session_id
              << ", client_address: " << client_address.toIpPort();
    return;
  }

  size_t packet_length = KCPPublicHeader::kPublicHeaderLength;
  public_header.checksum = static_cast<uint32_t>(
      ::adler32(1, reinterpret_cast<const Bytef*>(buf + 4),
                static_cast<uInt>(packet_length - 4)));
  if (!public_header.WriteChecksum(buf, sizeof(buf))) {
    return;
  }

  int rc = socket_->SendTo(buf, packet_length, client_address);
  if (rc < 0) {
    int saved_errno = -rc;
    LOG_ERROR << "SendTo failed, public header: " << public_header
              << ", client_address: " << client_address.toIpPort()
              << ", error: " << saved_errno
              << ", detail: " << muduo::strerror_tl(saved_errno);
  }
}

void KCPServer::ProcessSynPacket(
    const KCPPublicHeader& public_header, KCPReceivedPacket& packet,
    const muduo::net::InetAddress& client_address) {
  assert(packet.RemainingBytes() == 0);
  assert(public_header.packet_type == SYN_PACKET);
  assert(public_header.session_id == 0);

  UNUSED(public_header);
  UNUSED(packet);

  uint32_t session_id = 0;
  const std::string& session_key = client_address.toIpPort();
  auto it = pending_session_map_.find(session_key);
  if (it != pending_session_map_.end()) {
    std::unique_ptr<KCPPendingSession>& pending_session = it->second;
    if (pending_session->retry_times >= kServerMaxSynRetryTimes) {
      LOG_INFO << "server syn retry times reach limit, session_id: "
               << pending_session->session_id
               << ", client_address: " << client_address.toIpPort();
      SendPacket(RST_PACKET, 0, client_address);
      pending_session_map_.erase(it);
      return;
    }

    ++pending_session->retry_times;
    pending_session->syn_sent_time = muduo::Timestamp::now();

    session_id = pending_session->session_id;
  } else {
    if (!GenerateSessionId(&session_id)) {
      LOG_ERROR << "GenerateSessionId failed";
      SendPacket(RST_PACKET, 0, client_address);
      return;
    }

    auto pending_session = std::make_unique<KCPPendingSession>();
    pending_session->session_id = session_id;
    pending_session->syn_sent_time = pending_session->syn_received_time =
        muduo::Timestamp::now();
    pending_session->peer_address = client_address;
    auto result = pending_session_map_.insert(
        std::make_pair(session_key, std::move(pending_session)));
    if (!result.second) {
      return;
    }

    it = result.first;
  }

  SendPacket(SYN_PACKET, session_id, client_address);
}

void KCPServer::ProcessPingPacket(
    const KCPPublicHeader& public_header, KCPReceivedPacket& packet,
    const muduo::net::InetAddress& client_address) {
  assert(packet.RemainingBytes() == 0);
  assert(public_header.packet_type == PING_PACKET);
  assert(public_header.session_id > 0);

  UNUSED(public_header);
  UNUSED(packet);

  uint32_t session_id = public_header.session_id;
  auto session_it = session_map_.find(session_id);
  if (session_it == session_map_.end()) {
    LOG_ERROR << "received ping packet but session not exists, session_id: "
              << session_id
              << ", client_address: " << client_address.toIpPort();
    SendPacket(RST_PACKET, session_id, client_address);
    return;
  }

  auto idle_session_it = idle_session_map_.find(session_id);
  if (idle_session_it != idle_session_map_.end()) {
    idle_session_it->second = muduo::Timestamp::now();
  } else {
    LOG_WARN
        << "received ping packet, but idle session not exists, session_id: "
        << session_id << ", client_address: " << client_address.toIpPort();
    idle_session_map_.insert(
        std::make_pair(session_id, muduo::Timestamp::now()));
  }

  SendPacket(PONG_PACKET, session_id, client_address);
}

void KCPServer::ProcessAckPacket(
    const KCPPublicHeader& public_header, KCPReceivedPacket& packet,
    const muduo::net::InetAddress& client_address) {
  assert(packet.RemainingBytes() == 0);
  assert(public_header.packet_type == ACK_PACKET);

  UNUSED(public_header);
  UNUSED(packet);

  uint32_t session_id = public_header.session_id;
  const std::string& session_key = client_address.toIpPort();
  auto pending_session_it = pending_session_map_.find(session_key);
  if (pending_session_it == pending_session_map_.end()) {
    auto session_it = session_map_.find(session_id);
    if (session_it == session_map_.end()) {
      LOG_INFO << "session not exists, session_id: " << session_id
               << ", client_address: " << client_address.toIpPort();
      SendPacket(RST_PACKET, 0, client_address);
      return;
    }
    LOG_INFO << "session already connected, session_id: " << session_id
             << ", client_address: " << client_address.toIpPort();
  } else {
    std::unique_ptr<KCPPendingSession>& pending_session =
        pending_session_it->second;
    if (session_id != pending_session->session_id) {
      LOG_ERROR << "received ack packet with incorrect session_id: "
                << session_id
                << ", expected session_id: " << pending_session->session_id;
      SendPacket(RST_PACKET, 0, client_address);
      return;
    }

    auto session_it = session_map_.find(session_id);
    if (session_it != session_map_.end()) {
      LOG_WARN << "received ack packet from client_addres: "
               << client_address.toIpPort()
               << " but session already connected, session_id: " << session_id;
      return;
    }

    muduo::net::EventLoop* loop = thread_pool_->getLoopForHash(session_id);

    auto session = std::make_shared<KCPSession>(loop);
    if (!InitializeSession(session, session_id, client_address)) {
      LOG_ERROR << "initialize session failed, session_id: " << session_id
                << ", client_address: " << client_address.toIpPort();
      return;
    }

    auto result = session_map_.insert(std::make_pair(session_id, session));
    if (!result.second) {
      LOG_ERROR << "insert session failed, session_id: " << session_id
                << ", client_address: " << client_address.toIpPort();
      return;
    }
    pending_session_map_.erase(pending_session_it);

    // ignore result
    idle_session_map_.insert(
        std::make_pair(session_id, muduo::Timestamp::now()));
  }
}

void KCPServer::ProcessRstPacket(
    const KCPPublicHeader& public_header, KCPReceivedPacket& packet,
    const muduo::net::InetAddress& client_address) {
  assert(packet.RemainingBytes() == 0);
  assert(public_header.packet_type == RST_PACKET);

  UNUSED(public_header);
  UNUSED(packet);

  uint32_t session_id = public_header.session_id;
  pending_session_map_.erase(client_address.toIpPort());

  auto session_it = session_map_.find(session_id);
  if (session_it != session_map_.end()) {
    KCPSessionPtr& session = session_it->second;
    session->loop()->runInLoop([session] { session->Close(); });
    session_map_.erase(session_it);
    muduo::Timestamp now = muduo::Timestamp::now();
    time_wait_session_map_.insert(std::make_pair(session_id, now));
  }

  idle_session_map_.erase(session_id);
}

bool KCPServer::InitializeSession(
    KCPSessionPtr& session, uint32_t session_id,
    const muduo::net::InetAddress& client_address) {
  KCPSession::Params params = kFastModeKCPParams;
  params.head_room = KCPPublicHeader::kPublicHeaderLength;

  session->set_connection_callback(connection_callback_);
  session->set_message_callback(message_callback_);
  session->set_write_complete_callback(write_complete_callback_);
  session->set_high_water_mark_callback(high_water_mark_callback_);
  session->set_output_callback([this](const void* data, size_t len,
                                      const muduo::net::InetAddress& address) {
    AppendPacket(data, len, address);
  });
  session->set_flush_tx_queue([this] { FlushTxQueue(); });

  if (!session->Initialize(session_id, client_address, params)) {
    LOG_ERROR << "Initialize failed, session_id :" << session_id
              << ", client_address: " << client_address.toIpPort();
    return false;
  }

  return true;
}

void KCPServer::ProcessDataPacket(
    const KCPPublicHeader& public_header, KCPReceivedPacket& packet,
    const muduo::net::InetAddress& client_address) {
  uint32_t session_id = public_header.session_id;
  auto session_it = session_map_.find(session_id);
  if (session_it == session_map_.end()) {
    const std::string& pending_session_key = client_address.toIpPort();
    auto pending_session_it = pending_session_map_.find(pending_session_key);
    if (pending_session_it == pending_session_map_.end()) {
      LOG_ERROR << "received data packet but session not exists, session_id "
                << session_id;
      SendPacket(RST_PACKET, 0, client_address);
      return;
    }

    muduo::net::EventLoop* loop = thread_pool_->getLoopForHash(session_id);
    auto session = std::make_shared<KCPSession>(loop);
    if (!InitializeSession(session, session_id, client_address)) {
      LOG_ERROR << "InitializeSession failed, session_id: " << session_id
                << ", client_address: " << client_address.toIpPort();
      return;
    }

    auto result = session_map_.insert(std::make_pair(session_id, session));
    if (!result.second) {
      LOG_ERROR << "session insert failed, session_id :" << session_id
                << ", client_address: " << client_address.toIpPort();
      return;
    }
    session_it = result.first;
    pending_session_map_.erase(pending_session_it);

    // ignore result
    idle_session_map_.insert(
        std::make_pair(session_id, muduo::Timestamp::now()));
  }

  session_it->second->ProcessPacket(packet, client_address);
}

void KCPServer::ProcessPacket(KCPReceivedPacket& packet,
                              const muduo::net::InetAddress& client_address) {
  if (packet.length() > kMaxPacketSize) {
    LOG_ERROR << "received incorrect packet length: " << packet.length()
              << ", max length limit: " << kMaxPacketSize;
    return;
  }

  // decrypt
  // checksum
  // fec

  KCPPublicHeader public_header;
  if (!packet.ReadPublicHeader(&public_header)) {
    LOG_ERROR << "received incorrect packet length: " << packet.length()
              << ", can not read public header";
    return;
  }

  // checksum
  auto expected_checksum = static_cast<uint32_t>(
      ::adler32(1, reinterpret_cast<const Bytef*>(packet.data() + 4),
                static_cast<uInt>(packet.length() - 4)));
  if (public_header.checksum != expected_checksum) {
    LOG_ERROR << "received incorrect packet checksum: "
              << public_header.checksum << ", expected: " << expected_checksum;
    return;
  }

  switch (public_header.packet_type) {
    case SYN_PACKET: {
      ProcessSynPacket(public_header, packet, client_address);
      break;
    }
    case ACK_PACKET: {
      ProcessAckPacket(public_header, packet, client_address);
      break;
    }
    case RST_PACKET: {
      ProcessRstPacket(public_header, packet, client_address);
      break;
    }
    case PING_PACKET: {
      ProcessPingPacket(public_header, packet, client_address);
      break;
    }
    case DATA_PACKET: {
      ProcessDataPacket(public_header, packet, client_address);
      break;
    }
    default: {
      LOG_ERROR << "received unknown packet type: "
                << public_header.packet_type;
      return;
    }
  }
}

void KCPServer::InitializeThread(muduo::net::EventLoop*) const {
  auto& thread_data = muduo::ThreadLocalSingleton<ThreadData>::instance();

  thread_data.mmsg_hdrs = std::make_unique<mmsghdr[]>(kNumPacketsPerSend);
  thread_data.raw_packets = std::make_unique<RawPacket[]>(kNumPacketsPerSend);

  memset(thread_data.mmsg_hdrs.get(), 0, kNumPacketsPerSend * sizeof(mmsghdr));

  for (int i = 0; i < kNumPacketsPerSend; ++i) {
    auto pkt = &thread_data.raw_packets[i];
    pkt->iov.iov_base = pkt->buf;
    pkt->iov.iov_len = sizeof(pkt->buf);
    memset(&pkt->addr, 0, sizeof(pkt->addr));
    memset(pkt->buf, 0, sizeof(pkt->buf));

    auto hdr = &thread_data.mmsg_hdrs[i].msg_hdr;
    hdr->msg_name = &pkt->addr;
    hdr->msg_namelen = sizeof(sockaddr_storage);
    hdr->msg_iov = &pkt->iov;
    hdr->msg_iovlen = 1;
  }
}

void KCPServer::AppendPacket(
    const void* data, size_t len,
    const muduo::net::InetAddress& address) /* const */ {
  if (len > kMaxPacketSize) {
    LOG_ERROR << "AppendPacket with invalid data length: " << len
              << " address: " << address.toIpPort();
    return;
  }

  auto& thread_data = muduo::ThreadLocalSingleton<ThreadData>::instance();
  unsigned int index = thread_data.num_packets;

  assert(index < kNumPacketsPerSend);

  SockaddrStorage storage;
  if (!SockaddrStorage::ToSockAddr(address, &storage)) {
    LOG_ERROR << "AppendPacket with invalid address: " << address.toIpPort();
    return;
  }

  struct msghdr* hdr = &thread_data.mmsg_hdrs[index].msg_hdr;
  hdr->msg_namelen = storage.addr_len;
  memcpy(hdr->msg_name, storage.addr, storage.addr_len);

  RawPacket* pkt = &thread_data.raw_packets[index];
  pkt->iov.iov_len = len;
  memcpy(pkt->iov.iov_base, data, len);

  ++thread_data.num_packets;
  if (thread_data.num_packets >= kNumPacketsPerSend) {
    FlushTxQueue();
  }
}

void KCPServer::FlushTxQueue() /* const */ {
  auto& thread_data = muduo::ThreadLocalSingleton<ThreadData>::instance();

  unsigned int num_packets = thread_data.num_packets;
  if (num_packets == 0) {
    return;
  }

  assert(num_packets <= kNumPacketsPerSend);

  std::unique_ptr<mmsghdr[]>& mmsg_hdrs = thread_data.mmsg_hdrs;

  // thread safe
  // man 2 sendmmsg
  // An error is returned only if no datagrams could be sent.
  int rc = socket_->SendMmsg(mmsg_hdrs.get(), num_packets);
  if (rc < 0) {
    int saved_errno = -rc;
    if (IS_EAGAIN(saved_errno)) {
      // MutexLockGuard ...
      // SetWriteBlocked();
      // channel_->enableWriting();
      // cache packets unsent
    } else {
      // print thread local error
      LOG_ERROR << "SendMmsg error: " << saved_errno
                << ", detail: " << muduo::strerror_tl(saved_errno)
                << ", packets unsent: " << num_packets;
    }
  } else {
    auto packets_sent = static_cast<unsigned int>(rc);
    if (packets_sent < num_packets) {
      LOG_WARN << "FlushTxQueue total packets: " << num_packets
               << ", sent: " << packets_sent
               << ", unsent: " << (num_packets - packets_sent);
      // cache packets unsent
    }
  }

  thread_data.num_packets = 0;
}
