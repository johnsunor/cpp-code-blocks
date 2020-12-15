
#include "kcp_client.h"

#include <linux/errqueue.h>
#include <zconf.h>
#include <zlib.h>

#include <string.h>

#include <functional>
#include <memory>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/ThreadLocalSingleton.h>
#include <muduo/base/ThreadPool.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/InetAddress.h>

#include "common/macros.h"

#include "kcp_packets.h"
#include "kcp_session.h"
#include "udp_socket.h"

KCPClient::KCPClient(muduo::net::EventLoop* loop)
    : loop_(CHECK_NOTNULL(loop)) {}

KCPClient::~KCPClient() {
  Disconnect();

  // ~socket_
}

int KCPClient::Connect(const muduo::net::InetAddress& address) {
  loop_->assertInLoopThread();

  auto socket = std::make_unique<UDPSocket>();

  socket->AllowReceiveError();

  int rc = socket->Connect(address);
  if (rc < 0) {
    LOG_ERROR << "Connect error: " << rc;
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

  rc = socket->GetLocalAddress(&client_address_);
  if (rc < 0) {
    LOG_ERROR << "GetLocalAddress error: " << rc;
    return rc;
  }

  rc = socket->GetPeerAddress(&server_address_);
  if (rc < 0) {
    LOG_ERROR << "GetPeerAddress error: " << rc;
    return rc;
  }

  socket_ = std::move(socket);

  channel_ = std::make_unique<muduo::net::Channel>(loop_, socket_->sockfd());
  channel_->setReadCallback(
      [this](muduo::Timestamp receive_time) { HandleRead(receive_time); });
  channel_->setWriteCallback([this] { HandleWrite(); });
  channel_->setErrorCallback([this] { HandleError(); });
  channel_->enableReading();

  BuildSession();
  periodic_task_timer_ = loop_->runEvery(kClientRunPeriodicTaskInterval,
                                         [this] { RunPeriodicTask(); });

  return 0;
}

void KCPClient::ConnectOrDie(const muduo::net::InetAddress& address) {
  int rc = Connect(address);
  if (rc != 0) {
    LOG_FATAL << "KCPClient::ConnectOrDie";
  }
  LOG_INFO << "kcp client connecting to " << address.toIpPort();
}

void KCPClient::RunPeriodicTask() {
  muduo::Timestamp now = muduo::Timestamp::now();
  if (state_ == PENDING) {
    assert(pending_session_.get() != nullptr);
    if (pending_session_->retry_times >= kClientMaxSynRetryTimes) {
      LOG_ERROR << "session connecting failed because reach max retry times";
      ResetSession();
      return;
    }

    // send syn packet
    if (muduo::timeDifference(now, pending_session_->syn_sent_time) >=
        kClientSynSentTimeout) {
      ++pending_session_->retry_times;
      pending_session_->syn_sent_time = now;
      SendPacket(SYN_PACKET, pending_session_->session_id);
      return;
    }
  } else if (state_ == CONNECTED) {
    assert(session_.get() != nullptr);
    if (last_received_time_.valid() &&
        muduo::timeDifference(now, last_received_time_) >=
            kClientSessionIdleSeconds) {
      LOG_DEBUG << "session receive server response timeout, session_id: "
                << session_->session_id();
      ResetSession();
    } else if (session_->IsClosed()) {
      LOG_DEBUG << "session has closed, session_id: " << session_->session_id();
      ResetSession();
    } else if (last_ping_time_.valid() &&
               muduo::timeDifference(now, last_ping_time_) >=
                   kClientPingInterval) {
      LOG_DEBUG << "time to send ping packet, session_id: "
                << session_->session_id();
      SendPacket(PING_PACKET, session_->session_id());
    }
  }
}

void KCPClient::BuildSession() {
  if (!socket_->IsValidSocket()) {
    LOG_WARN << "socket has closed at " << this;
    return;
  }

  if (state_ == CLOSED) {
    SendPacket(SYN_PACKET, 0);
    pending_session_ = std::make_unique<KCPPendingSession>();
    pending_session_->syn_sent_time = muduo::Timestamp::now();
    set_state(PENDING);
  }
}

void KCPClient::ResetSession() {
  LOG_INFO << "reset session from state: " << state_ << " at " << this;

  if (pending_session_) {
    pending_session_.reset();
  }

  if (session_) {
    session_->Close();
    session_.reset();
  }

  set_state(CLOSED);

  // check if socket_ has closed
  if (!socket_->IsValidSocket()) {
    LOG_INFO << "skip reconnect because socket has closed";
    return;
  }

  // check if reconnect enabled
  if (!reconnect_enabled_) {
    LOG_INFO << "skip reconnect because reconnect disabled";
    return;
  }

  // check if timer has registered
  if (reconnect_timer_registered_) {
    LOG_INFO << "skip reconnect because reconnect timer has registered";
    return;
  }

  ++reconnect_times_;
  // delay seconds
  double retry_delay =
      std::min(reconnect_times_ * 0.5, kClientMaxReconnectDelay);

  reconnect_timer_registered_ = true;
  reconnect_timer_ = loop_->runAfter(retry_delay, [this] {
    BuildSession();
    reconnect_timer_registered_ = false;
  });

  LOG_INFO << "reconnect after " << retry_delay << " seconds at " << this;
}

// manually close
void KCPClient::Disconnect() {
  loop_->assertInLoopThread();

  if (state_ == PENDING) {
    assert(pending_session_.get() != nullptr);
    pending_session_.reset();
    LOG_INFO << "disconnect from state PENDING";
  } else if (state_ == CONNECTED) {
    assert(session_.get() != nullptr);
    session_->Close(true);
    SendPacket(RST_PACKET, session_->session_id());
    session_.reset();
    LOG_INFO << "disconnect from state CONNECTED";
  } else {
    LOG_INFO << "disconnect from state CLOSED";
  }

  loop_->cancel(periodic_task_timer_);
  loop_->cancel(reconnect_timer_);
  reconnect_timer_registered_ = false;

  if (channel_) {
    channel_->disableAll();
    channel_->remove();
    channel_.reset();
  }

  if (socket_) {
    socket_->Close();
    socket_.reset();
  }

  set_state(CLOSED);
}

void KCPClient::HandleRead(muduo::Timestamp) {
  if (!(socket_ && socket_->IsValidSocket())) {
    return;
  }

  char buf[kMaxPacketSize + 1];
  int rc = socket_->Read(buf, sizeof(buf));
  if (rc < 0) {
    int last_error = -rc;
    if (!IS_EAGAIN(last_error)) {
      LOG_ERROR << "Read failed with error: " << last_error
                << ", detail: " << muduo::strerror_tl(last_error);
    }
    return;
  }

  if (rc > kMaxPacketSize) {
    LOG_ERROR << "HandleRead length of received packet exceeds limit";
    return;
  }

  KCPReceivedPacket packet(buf, rc);
  ProcessPacket(packet);
}

void KCPClient::HandleWrite() {
  // SetWritable();
  //
  // for (auto& packet: queued_packets_)
  // }
  //
  // if (queued_packets_.empty()) {
  //   channel_->disableWriting();
  // }
}

void KCPClient::HandleError() {
  char data[kMaxPacketSize + 1];
  struct iovec iov;
  iov.iov_base = data;
  iov.iov_len = sizeof(data);

  struct msghdr msg;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_name = nullptr;
  msg.msg_namelen = 0;

  char buf[kMaxAncillaryDataLength];
  msg.msg_control = buf;
  msg.msg_controllen = sizeof(buf);
  msg.msg_flags = 0;

  while (true) {
    if (!socket_->IsValidSocket()) {
      return;
    }

    msg.msg_namelen = 0;
    msg.msg_controllen = sizeof(buf);
    msg.msg_flags = 0;
    int rc = socket_->RecvMsg(&msg, MSG_ERRQUEUE);

    if (rc < 0) {
      int saved_errno = -rc;
      if (!IS_EAGAIN(saved_errno)) {
        LOG_ERROR << "RecvMsg failed with error: " << saved_errno
                  << ", detail: " << muduo::strerror_tl(saved_errno);
      }
      break;
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
        LOG_ERROR << "received error: " << serr->ee_errno
                  << ", orignal: " << serr->ee_origin
                  << ", type: " << serr->ee_type
                  << ", detail: " << muduo::strerror_tl(serr->ee_errno);
        if (error_message_callback_) {
          error_message_callback_(*cmsg);
        } else if (serr->ee_origin == SO_EE_ORIGIN_ICMP ||
                   serr->ee_origin == SO_EE_ORIGIN_ICMP6) {
          KCPPublicHeader public_header;
          if (public_header.ReadFrom(data, rc) && session_) {
            PendingError pending_error = {.type = serr->ee_type,
                                          .code = serr->ee_code};
            session_->set_pending_error(pending_error);
          } else {
            LOG_ERROR << "HandleError read public header failed";
          }
          LOG_ERROR << "received icmp error: " << serr->ee_errno
                    << ", type: " << serr->ee_type
                    << ", code: " << serr->ee_code << ", orignal packet info: "
                    << "{ length: " << rc
                    << ", public header: " << public_header << " }";
        }

        if (!socket_->IsValidSocket()) {
          return;
        }
      }
    }
  }
}

void KCPClient::SendPacket(uint8_t packet_type, uint32_t session_id) {
  assert(socket_->IsValidSocket());

  KCPPublicHeader public_header;

  public_header.packet_type = packet_type;
  public_header.session_id = session_id;

  char buf[kMaxPacketSize];
  if (!public_header.WriteTo(buf, sizeof(buf))) {
    LOG_ERROR << "SendPacket write to buffer failed";
    return;
  }

  size_t packet_length = KCPPublicHeader::kPublicHeaderLength;
  public_header.checksum = static_cast<uint32_t>(
      ::adler32(1, reinterpret_cast<const Bytef*>(buf + 4),
                static_cast<uInt>(packet_length - 4)));
  if (!public_header.WriteChecksum(buf, sizeof(buf))) {
    return;
  }

  int rc = socket_->Write(buf, packet_length);
  if (rc < 0) {
    int saved_errno = -rc;
    LOG_ERROR << "SendPacket failed, public header: " << public_header
              << ", server_address: " << server_address_.toIpPort()
              << ", error: " << saved_errno
              << ", detail: " << muduo::strerror_tl(saved_errno);
  }
}

void KCPClient::ProcessSynPacket(const KCPPublicHeader& public_header,
                                 KCPReceivedPacket& packet) {
  assert(packet.RemainingBytes() == 0);
  assert(public_header.packet_type == SYN_PACKET);
  assert(public_header.session_id > 0);

  UNUSED(packet);

  auto session_id = public_header.session_id;
  if (session_.get() != nullptr) {
    if (session_->session_id() != session_id) {
      LOG_ERROR << "received syn packet with unexpected session_id: "
                << session_id
                << ", server_address: " << server_address_.toIpPort();
      SendPacket(RST_PACKET, session_id);
      return;
    }

    LOG_INFO << "session has connected, session_id: " << session_id;
    SendPacket(ACK_PACKET, session_id);
    return;
  }

  if (pending_session_.get() == nullptr) {
    LOG_ERROR << "received server syn packet but pending session not exists, "
                 "server_address: "
              << server_address_.toIpPort();
    SendPacket(RST_PACKET, session_id);
    return;
  }

  auto params = kFastModeKCPParams;
  params.head_room = KCPPublicHeader::kPublicHeaderLength;

  // client can send data packet in "connection_callback_" as ack packet
  auto session = std::make_shared<KCPSession>(loop_);
  session->set_connection_callback(connection_callback_);
  session->set_message_callback(message_callback_);
  session->set_write_complete_callback(write_complete_callback_);
  session->set_output_callback([this](const void* data, size_t len,
                                      const muduo::net::InetAddress& address) {
    SendDataToWire(data, len, address);
  });
  if (!session->Initialize(session_id, server_address_, params)) {
    LOG_ERROR << "session initialize failed, session_id :" << session_id
              << ", server_address: " << server_address_.toIpPort();
    return;
  }

  session_ = std::move(session);
  pending_session_.reset();
  last_ping_time_ = muduo::Timestamp::now();
  set_state(CONNECTED);

  // send normal ack packet
  SendPacket(ACK_PACKET, session_id);

  LOG_INFO << "session connected, server_address: "
           << server_address_.toIpPort() << ", session_id: " << session_id;
}

void KCPClient::ProcessRstPacket(const KCPPublicHeader& public_header,
                                 KCPReceivedPacket& packet) {
  assert(packet.RemainingBytes() == 0);
  assert(public_header.packet_type == RST_PACKET);

  UNUSED(packet);

  auto session_id = public_header.session_id;

  LOG_INFO << "received rst packet, server_address: "
           << server_address_.toIpPort() << ", session_id: " << session_id;

  ResetSession();
}

void KCPClient::ProcessPongPacket(const KCPPublicHeader& public_header,
                                  KCPReceivedPacket& packet) {
  assert(packet.RemainingBytes() == 0);
  assert(public_header.packet_type == PONG_PACKET);
  assert(public_header.session_id > 0);

  UNUSED(packet);

  auto session_id = public_header.session_id;
  if (session_.get() == nullptr) {
    LOG_ERROR << "received pong packet but session not exists, server_address: "
              << server_address_.toIpPort() << ", session_id: " << session_id;
    SendPacket(RST_PACKET, session_id);
    return;
  }

  LOG_INFO << "received pong packet, server_address: "
           << server_address_.toIpPort() << ", session_id: " << session_id;

  last_received_time_ = muduo::Timestamp::now();
}

void KCPClient::ProcessDataPacket(const KCPPublicHeader& public_header,
                                  KCPReceivedPacket& packet) {
  assert(public_header.packet_type == DATA_PACKET);
  assert(public_header.session_id > 0);

  UNUSED(packet);

  auto session_id = public_header.session_id;
  if (session_.get() == nullptr) {
    LOG_ERROR << "received data packet but session not exists, server_address: "
              << server_address_.toIpPort() << ", session_id: " << session_id;
    SendPacket(RST_PACKET, session_id);
    return;
  }

  LOG_DEBUG << "received data packet, server_address: "
            << server_address_.toIpPort() << ", session_id: " << session_id;

  last_received_time_ = muduo::Timestamp::now();
  session_->ProcessPacket(packet, server_address_);
}

void KCPClient::ProcessPacket(KCPReceivedPacket& packet) {
  assert(packet.length() <= kMaxPacketSize);

  // decrypt
  KCPPublicHeader public_header;
  if (!packet.ReadPublicHeader(&public_header)) {
    LOG_ERROR << "ProcessPacket read public header failed";
    return;
  }

  // check sum
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
      ProcessSynPacket(public_header, packet);
      break;
    }
    case RST_PACKET: {
      ProcessRstPacket(public_header, packet);
      break;
    }
    case PONG_PACKET: {
      ProcessPongPacket(public_header, packet);
      break;
    }
    case DATA_PACKET: {
      ProcessDataPacket(public_header, packet);
      break;
    }
    default: {
      LOG_ERROR << "received unknown packet type: "
                << public_header.packet_type;
      return;
    }
  }
}

void KCPClient::SendDataToWire(
    const void* data, size_t len,
    const muduo::net::InetAddress& address) /* const */ {
  UNUSED(address);
  if (!socket_->IsValidSocket()) {
    return;
  }

  if (len > kMaxPacketSize) {
    LOG_ERROR << "SendDataToWire with invalid data length: " << len;
    return;
  }

  int rc = socket_->Write(data, len);
  if (rc < 0) {
    int last_error = -rc;
    if (IS_EAGAIN(last_error)) {
      // cache data
      // channel_->enableWriting();
    } else {
      LOG_ERROR << "SendDataToWire error: " << last_error
                << ", detail: " << muduo::strerror_tl(last_error);
    }
  }
}
