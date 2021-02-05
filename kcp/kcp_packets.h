
#ifndef KCP_PACKETS_H_
#define KCP_PACKETS_H_

#include <memory>
#include <string>

#include "common/macros.h"

namespace muduo {

class LogStream;
}  // namespace muduo

enum KCPReceivedPacketType : uint8_t {
  SYN_PACKET,
  ACK_PACKET,
  RST_PACKET,
  PING_PACKET,
  PONG_PACKET,
  DATA_PACKET,
  NUM_PACKET_TYPES
};

struct KCPPublicHeader {
  // KCPPublicHeader() = default;
  // KCPPublicHeader(const KCPPublicHeader&) = default;
  //~KCPPublicHeader() = default;

  // serialize
  bool ReadFrom(const char* buf, size_t length);
  bool WriteTo(char* buf, size_t length) const;
  bool WriteChecksum(char* buf, size_t length) const;

  static std::string PacketTypeToString(uint8_t packet_type);

  uint32_t checksum{0};
  uint8_t packet_type{0};
  uint32_t session_id{0};

  static const size_t kPublicHeaderLength =
      sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);
};

muduo::LogStream& operator<<(muduo::LogStream& s,
                             const KCPPublicHeader& header);

class KCPReceivedPacket final {
 public:
  enum ErrorCode : uint8_t {
    SUCCESS,
    INVALID_LENGTH,
    UNABLE_READ_CHECKSUM,
    UNABLE_READ_PACKET_TYPE,
    UNABLE_READ_SESSION_ID,
    INVALID_CHECKSUM,
  };

  KCPReceivedPacket(const char* data, size_t length);
  KCPReceivedPacket(const char* data, size_t length, bool owns_data);

  ~KCPReceivedPacket();

  std::unique_ptr<KCPReceivedPacket> Clone() const;
  std::unique_ptr<KCPReceivedPacket> CloneFromRemainingData() const;

  bool ReadUInt8(uint8_t* result);
  bool ReadUInt16(uint16_t* result);
  bool ReadUInt32(uint32_t* result);
  bool ReadUInt64(uint64_t* result);
  bool CanRead(size_t bytes) const;
  bool ReadBytes(void* result, size_t size);

  bool PeekUInt8(uint8_t* result) const;
  bool PeekUInt16(uint16_t* result) const;
  bool PeekUInt32(uint32_t* result) const;
  bool PeekUInt64(uint64_t* result) const;
  bool CanPeek(size_t bytes) const;
  bool PeekBytes(void* result, size_t size) const;

  size_t RemainingBytes() const { return length_ - pos_; }
  const char* RemainingData() const { return data_ + pos_; }

  const char* data() const { return data_; }
  size_t length() const { return length_; }

  ErrorCode ReadPublicHeader(KCPPublicHeader* public_header);

  static std::string ErrorCodeToString(uint8_t code);

 private:
  const char* data_{nullptr};
  size_t length_{0};
  size_t pos_{0};
  bool owns_data_{false};

  DISALLOW_COPY_AND_ASSIGN(KCPReceivedPacket);
};

muduo::LogStream& operator<<(muduo::LogStream& s,
                             const KCPReceivedPacket& packet);

// helper class for cloning data
class KCPClonedPacket final {
 public:
  KCPClonedPacket(const void* data, size_t length);
  ~KCPClonedPacket();

  char* data() { return data_; }
  size_t length() const { return length_; }

 private:
  char* data_{nullptr};
  size_t length_{0};

  DISALLOW_COPY_AND_ASSIGN(KCPClonedPacket);
};

class KCPPendingSendPacket final {
 public:
  enum ErrorCode : uint8_t {
    SUCCESS,
    INVALID_LENGTH,
    UNABLE_WRITE_PUBLIC_HEADER,
    UNABLE_WRITE_CHECKSUM,
  };

  KCPPendingSendPacket(char* data, size_t length);
  KCPPendingSendPacket(char* data, size_t length, bool owns_data);

  ~KCPPendingSendPacket();

  char* data() { return data_; }
  const char* data() const { return data_; }
  size_t length() const { return length_; }

  ErrorCode WritePublicHeader(uint8_t packet_type, uint32_t session_id);

  static std::string ErrorCodeToString(uint8_t code);

 private:
  char* data_{nullptr};
  size_t length_{0};
  bool owns_data_{false};

  DISALLOW_COPY_AND_ASSIGN(KCPPendingSendPacket);
};

#endif
