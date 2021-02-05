
#include "kcp_packets.h"

#include <zconf.h>
#include <zlib.h>

#include <memory>

#include <muduo/base/LogStream.h>

const size_t KCPPublicHeader::kPublicHeaderLength;

bool KCPPublicHeader::ReadFrom(const char* buf, size_t length) {
  assert(buf != nullptr);

  if (length < kPublicHeaderLength) {
    return false;
  }

  size_t offset = 0;

  // le32
  memcpy(&checksum, buf, sizeof(checksum));
  checksum = le32toh(checksum);
  offset += sizeof(checksum);

  // uint8_t
  memcpy(&packet_type, buf + offset, sizeof(packet_type));
  offset += sizeof(packet_type);

  // le32
  memcpy(&session_id, buf + offset, sizeof(session_id));
  session_id = le32toh(session_id);

  return true;
}

bool KCPPublicHeader::WriteTo(char* buf, size_t length) const {
  assert(buf != nullptr);

  if (length < kPublicHeaderLength) {
    return false;
  }

  size_t offset = 0;

  uint32_t le32 = htole32(checksum);
  memcpy(buf + offset, &le32, sizeof(checksum));
  offset += sizeof(checksum);

  // uint8_t
  memcpy(buf + offset, &packet_type, sizeof(packet_type));
  offset += sizeof(packet_type);

  le32 = htole32(session_id);
  memcpy(buf + offset, &le32, sizeof(session_id));
  offset += sizeof(session_id);

  return true;
}

bool KCPPublicHeader::WriteChecksum(char* buf, size_t length) const {
  assert(buf != nullptr);

  if (length < sizeof(checksum)) {
    return false;
  }

  uint32_t le32 = htole32(checksum);
  memcpy(buf, &le32, sizeof(checksum));

  return true;
}

#define PACKET_TYPE_CASE(packet_type) \
  case packet_type:                   \
    return #packet_type

std::string KCPPublicHeader::PacketTypeToString(uint8_t packet_type) {
  switch (packet_type) {
    PACKET_TYPE_CASE(SYN_PACKET);
    PACKET_TYPE_CASE(ACK_PACKET);
    PACKET_TYPE_CASE(RST_PACKET);
    PACKET_TYPE_CASE(PING_PACKET);
    PACKET_TYPE_CASE(DATA_PACKET);
    default:
      return "UNKNOW";
  }
}

#undef PACKET_TYPE_CASE

muduo::LogStream& operator<<(muduo::LogStream& s,
                             const KCPPublicHeader& header) {
  s << "{ packet_type: " << header.packet_type << "("
    << KCPPublicHeader::PacketTypeToString(header.packet_type) << ")"
    << ", session_id: " << header.session_id << " }";
  return s;
}

KCPReceivedPacket::KCPReceivedPacket(const char* data, size_t length)
    : KCPReceivedPacket(data, length, false) {}

KCPReceivedPacket::KCPReceivedPacket(const char* data, size_t length,
                                     bool owns_data)
    : data_(data), length_(length), pos_(0), owns_data_(owns_data) {}

KCPReceivedPacket::~KCPReceivedPacket() {
  if (owns_data_) {
    delete[] const_cast<char*>(data_);
  }
}

std::unique_ptr<KCPReceivedPacket> KCPReceivedPacket::Clone() const {
  char* buffer = new char[this->length()];
  memcpy(buffer, this->data(), this->length());
  return std::make_unique<KCPReceivedPacket>(buffer, this->length(), true);
}

std::unique_ptr<KCPReceivedPacket> KCPReceivedPacket::CloneFromRemainingData()
    const {
  char* buffer = new char[this->RemainingBytes()];
  memcpy(buffer, this->RemainingData(), this->RemainingBytes());
  return std::make_unique<KCPReceivedPacket>(buffer, this->RemainingBytes(),
                                             true);
}

bool KCPReceivedPacket::ReadUInt8(uint8_t* result) {
  return ReadBytes(result, sizeof(*result));
}

bool KCPReceivedPacket::ReadUInt16(uint16_t* result) {
  assert(result != nullptr);

  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }

  *result = le16toh(*result);
  return true;
}

bool KCPReceivedPacket::ReadUInt32(uint32_t* result) {
  assert(result != nullptr);

  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }

  *result = le32toh(*result);
  return true;
}

bool KCPReceivedPacket::ReadUInt64(uint64_t* result) {
  assert(result != nullptr);

  if (!ReadBytes(result, sizeof(*result))) {
    return false;
  }

  *result = le64toh(*result);
  return true;
}

bool KCPReceivedPacket::CanRead(size_t bytes) const {
  return bytes <= (length_ - pos_);
}

bool KCPReceivedPacket::ReadBytes(void* result, size_t size) {
  assert(result != nullptr);

  if (!CanRead(size)) {
    return false;
  }

  // Read into result.
  memcpy(result, data_ + pos_, size);

  // Iterate.
  pos_ += size;

  return true;
}

bool KCPReceivedPacket::PeekUInt8(uint8_t* result) const {
  return PeekBytes(result, sizeof(*result));
}

bool KCPReceivedPacket::PeekUInt16(uint16_t* result) const {
  assert(result != nullptr);

  if (!PeekBytes(result, sizeof(*result))) {
    return false;
  }

  *result = le16toh(*result);
  return true;
}

bool KCPReceivedPacket::PeekUInt32(uint32_t* result) const {
  assert(result != nullptr);

  if (!PeekBytes(result, sizeof(*result))) {
    return false;
  }

  *result = le32toh(*result);
  return true;
}

bool KCPReceivedPacket::PeekUInt64(uint64_t* result) const {
  assert(result != nullptr);

  if (!PeekBytes(result, sizeof(*result))) {
    return false;
  }

  *result = le64toh(*result);
  return true;
}

bool KCPReceivedPacket::CanPeek(size_t bytes) const {
  return bytes <= (length_ - pos_);
}

bool KCPReceivedPacket::PeekBytes(void* result, size_t size) const {
  assert(result != nullptr);

  if (!CanPeek(size)) {
    return false;
  }

  memcpy(result, data_ + pos_, size);

  return true;
}

KCPReceivedPacket::ErrorCode KCPReceivedPacket::ReadPublicHeader(
    KCPPublicHeader* public_header) {
  assert(public_header != nullptr);

  if (!CanRead(KCPPublicHeader::kPublicHeaderLength)) {
    return INVALID_LENGTH;
  }

  uint32_t checksum;
  if (!ReadUInt32(&checksum)) {
    return UNABLE_READ_CHECKSUM;
  }

  uint8_t packet_type;
  if (!ReadUInt8(&packet_type)) {
    return UNABLE_READ_PACKET_TYPE;
  }

  uint32_t session_id;
  if (!ReadUInt32(&session_id)) {
    return UNABLE_READ_SESSION_ID;
  }

  // crc32/adler32
  auto expected_checksum = static_cast<uint32_t>(
      ::adler32(1, reinterpret_cast<const Bytef*>(data() + 4),
                static_cast<uInt>(length() - 4)));
  if (checksum != expected_checksum) {
    return INVALID_CHECKSUM;
  }

  public_header->checksum = checksum;
  public_header->packet_type = packet_type;
  public_header->session_id = session_id;

  return SUCCESS;
}

#define ERROR_CODE_CASE(code) \
  case code:                  \
    return #code

std::string KCPReceivedPacket::ErrorCodeToString(uint8_t code) {
  switch (code) {
    ERROR_CODE_CASE(SUCCESS);
    ERROR_CODE_CASE(INVALID_LENGTH);
    ERROR_CODE_CASE(UNABLE_READ_CHECKSUM);
    ERROR_CODE_CASE(UNABLE_READ_PACKET_TYPE);
    ERROR_CODE_CASE(UNABLE_READ_SESSION_ID);
    ERROR_CODE_CASE(INVALID_CHECKSUM);
    default:
      return "UNKNOW";
  }
}

#undef ERROR_CODE_CASE

muduo::LogStream& operator<<(muduo::LogStream& s,
                             const KCPReceivedPacket& packet) {
  s << packet.length() << "-byte data";
  return s;
}

KCPClonedPacket::KCPClonedPacket(const void* data, size_t length)
    : data_(new char[length]), length_(length) {
  memcpy(data_, data, length);
}

KCPClonedPacket::~KCPClonedPacket() { delete[] data_; }

KCPPendingSendPacket::KCPPendingSendPacket(char* data, size_t length)
    : KCPPendingSendPacket(data, length, false) {}

KCPPendingSendPacket::KCPPendingSendPacket(char* data, size_t length,
                                           bool owns_data)
    : data_(data), length_(length), owns_data_(owns_data) {}

KCPPendingSendPacket::~KCPPendingSendPacket() {
  if (owns_data_) {
    delete[] data_;
  }
}

KCPPendingSendPacket::ErrorCode KCPPendingSendPacket::WritePublicHeader(
    uint8_t packet_type, uint32_t session_id) {
  KCPPublicHeader public_header;

  public_header.packet_type = packet_type;
  public_header.session_id = session_id;
  if (!public_header.WriteTo(data(), length())) {
    return UNABLE_WRITE_PUBLIC_HEADER;
  }

  public_header.checksum = static_cast<uint32_t>(
      ::adler32(1, reinterpret_cast<const Bytef*>(data() + 4),
                static_cast<uInt>(length() - 4)));
  if (!public_header.WriteChecksum(data(), length())) {
    return UNABLE_WRITE_CHECKSUM;
  }

  return SUCCESS;
}

#define ERROR_CODE_CASE(code) \
  case code:                  \
    return #code

std::string KCPPendingSendPacket::ErrorCodeToString(uint8_t code) {
  switch (code) {
    ERROR_CODE_CASE(SUCCESS);
    ERROR_CODE_CASE(INVALID_LENGTH);
    ERROR_CODE_CASE(UNABLE_WRITE_PUBLIC_HEADER);
    ERROR_CODE_CASE(UNABLE_WRITE_CHECKSUM);
    default:
      return "UNKNOW";
  }
}

#undef ERROR_CODE_CASE
