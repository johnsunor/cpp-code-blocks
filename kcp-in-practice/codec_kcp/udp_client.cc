
#include "udp_client.h"

#include <muduo/base/Logging.h>
#include <muduo/net/SocketsOps.h>

void UDPClient::Start() {
  assert(socket_.sockfd() != kInvalidSocket);
  assert(!channel_.get());

  channel_.reset(new muduo::net::Channel(loop_, socket_.sockfd()));
  channel_->setReadCallback(boost::bind(&UDPClient::HandleRead, this, _1));
  channel_->setWriteCallback(boost::bind(&UDPClient::HandleWrite, this));
  channel_->setErrorCallback(boost::bind(&UDPClient::HandleError, this));
  channel_->enableReading();
}

int UDPClient::Connect(const muduo::net::InetAddress& address) {
  assert(!socket_.is_connected());

  int result = socket_.Connect(address);
  if (result < 0) {
    LOG_SYSERR << "UDPClient::Connect - connect to " << address.toIpPort()
               << " failed, "
               << " error = " << muduo::strerror_tl(result);
  }

  return result;
}

void UDPClient::HandleRead(muduo::Timestamp receive_time) {
  assert(socket_.sockfd() != kInvalidSocket);
  assert(read_buf_.writableBytes() > 0);

  int bytes_transferred =
      socket_.Read(read_buf_.beginWrite(), read_buf_.writableBytes());
  if (bytes_transferred > 0) {
    read_buf_.hasWritten(bytes_transferred);
    if (message_callback_) {
      message_callback_(&read_buf_, receive_time);
    }

    read_buf_.retrieveAll();
  } else if (bytes_transferred < 0) {
    errno = bytes_transferred;
    LOG_SYSERR << "UDPClient::handleRead";
    HandleError();
  }
}

int UDPClient::Write(const void* buf, size_t len) {
  int bytes_transferred = socket_.Write(buf, len);
  if (bytes_transferred < 0) {
    if (bytes_transferred == EAGAIN || bytes_transferred == EWOULDBLOCK) {
      SetWriteBlocked();
    }

    errno = bytes_transferred;
    LOG_SYSERR << "UDPClient::SendTo";

    HandleError();
  }

  return bytes_transferred;
}

int UDPClient::Write(muduo::net::Buffer* buf) {
  int bytes_transferred = socket_.Write(buf->peek(), buf->readableBytes());
  if (bytes_transferred >= 0) {
    buf->retrieve(bytes_transferred);
  } else {
    errno = bytes_transferred;
    LOG_SYSERR << "UDPClient::SendTo";

    HandleError();
  }

  return bytes_transferred;
}

void UDPClient::WriteOrQueuePcket(const void* buf, size_t len) {
  assert(socket_.sockfd() != kInvalidSocket);

  if (IsWriteBlocked()) {
    queued_packets_.push_back(new QueuedPacket(buf, len));
    return;
  }

  int bytes_transferred = socket_.Write(buf, len);
  if (bytes_transferred < 0) {
    if (bytes_transferred == EAGAIN || bytes_transferred == EWOULDBLOCK) {
      SetWriteBlocked();
      channel_->enableWriting();
      queued_packets_.push_back(new QueuedPacket(buf, len));
    }

    errno = bytes_transferred;
    LOG_SYSERR << "UDPClient::SendTo";

    HandleError();
  }
}

void UDPClient::HandleWrite() {
  SetWritable();

  boost::ptr_list<QueuedPacket>::iterator packet_iterator =
      queued_packets_.begin();
  while (!IsWriteBlocked() && packet_iterator != queued_packets_.end()) {
    if (Write(packet_iterator->data, packet_iterator->size) >= 0) {
      queued_packets_.erase(packet_iterator);
    } else {
      ++packet_iterator;
    }
  }
}

void UDPClient::HandleError() {
  int err = muduo::net::sockets::getSocketError(channel_->fd());
  LOG_ERROR << "UDPClient::HandleError [fd:" << socket_.sockfd()
            << "] - SO_ERROR = " << err << " " << muduo::strerror_tl(err);
}
