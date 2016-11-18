
#include "udp_server.h"

#include <muduo/base/Logging.h>

void UDPServer::Start() {
  assert(socket_.sockfd() != kInvalidSocket);
  assert(!channel_.get());

  channel_.reset(new muduo::net::Channel(loop_, socket_.sockfd()));
  channel_->setReadCallback(boost::bind(&UDPServer::HandleRead, this, _1));
  channel_->setWriteCallback(boost::bind(&UDPServer::HandleWrite, this));
  channel_->setErrorCallback(boost::bind(&UDPServer::HandleError, this));
  channel_->enableReading();
}

void UDPServer::HandleRead(muduo::Timestamp receive_time) {
  assert(socket_.sockfd() != kInvalidSocket);
  assert(read_buf_.writableBytes() > 0);

  muduo::net::InetAddress address;
  int bytes_transferred = socket_.RecvFrom(read_buf_.beginWrite(),
                                           read_buf_.writableBytes(), &address);
  if (bytes_transferred > 0) {
    read_buf_.hasWritten(bytes_transferred);
    if (message_callback_) {
      message_callback_(&read_buf_, receive_time, address);
    }

    read_buf_.retrieveAll();
  } else if (bytes_transferred < 0) {
    errno = bytes_transferred;
    LOG_SYSERR << "UDPServer::handleRead";
    HandleError();
  }
}

int UDPServer::SendTo(const void* buf, size_t len,
                      const muduo::net::InetAddress& address) {
  int bytes_transferred = socket_.SendTo(buf, len, address);
  if (bytes_transferred < 0) {
    if (bytes_transferred == EAGAIN || bytes_transferred == EWOULDBLOCK) {
      SetWriteBlocked();
    }

    errno = bytes_transferred;
    LOG_SYSERR << "UDPServer::SendTo";

    HandleError();
  }

  return bytes_transferred;
}

int UDPServer::SendTo(muduo::net::Buffer* buf,
                      const muduo::net::InetAddress& address) {
  int bytes_transferred =
      socket_.SendTo(buf->peek(), buf->readableBytes(), address);
  if (bytes_transferred >= 0) {
    buf->retrieve(bytes_transferred);
  } else {
    errno = bytes_transferred;
    LOG_SYSERR << "UDPServer::SendTo";

    HandleError();
  }

  return bytes_transferred;
}

int UDPServer::SendOrQueuePcket(const void* buf, size_t len,
                                const muduo::net::InetAddress& address) {
  assert(len > 0);
  assert(socket_.sockfd() != kInvalidSocket);

  if (len > max_packet_size_) {
    LOG_ERROR << "UDPServer: send packet size too long";
    return -1;
  }

  if (IsWriteBlocked()) {
    queued_packets_.push_back(new QueuedPacket(buf, len, address));
    return 0;
  }

  int bytes_transferred = socket_.SendTo(buf, len, address);
  if (bytes_transferred >= 0) {
    return bytes_transferred;
  }

  if (bytes_transferred < 0) {
    if (bytes_transferred == EAGAIN || bytes_transferred == EWOULDBLOCK) {
      SetWriteBlocked();
      channel_->enableWriting();
      queued_packets_.push_back(new QueuedPacket(buf, len, address));
    }

    errno = bytes_transferred;
    LOG_SYSERR << "UDPServer::SendTo";

    HandleError();
  }

  return bytes_transferred;
}

void UDPServer::HandleWrite() {
  SetWritable();

  boost::ptr_list<QueuedPacket>::iterator packet_iterator =
      queued_packets_.begin();
  while (!IsWriteBlocked() && packet_iterator != queued_packets_.end()) {
    if (SendTo(packet_iterator->data, packet_iterator->size,
               packet_iterator->peer_addr) >= 0) {
      queued_packets_.erase(packet_iterator);
    } else {
      ++packet_iterator;
    }
  }
}

void UDPServer::HandleError() {
  int err = muduo::net::sockets::getSocketError(channel_->fd());
  LOG_ERROR << "UDPServer::HandleError [fd:" << socket_.sockfd()
            << "] - SO_ERROR = " << err << " " << muduo::strerror_tl(err);
}