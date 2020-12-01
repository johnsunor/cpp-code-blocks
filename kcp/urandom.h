
#ifndef URANDOM_H_
#define URANDOM_H_

#include <fcntl.h>
#include <unistd.h>

#include <muduo/base/Logging.h>
// #include <muduo/base/Singleton.h>

#include "common/macros.h"

class URandom final {
 public:
  static URandom& GetInstance() {
    // return muduo::Singleton<URandom>::instance();
    static URandom instance;
    return instance;
  }

  bool RandBytes(void* buf, size_t bytes) {
    size_t total_read = 0;
    while (total_read < bytes) {
      void* addr = static_cast<char*>(buf) + total_read;
      ssize_t bytes_read = HANDLE_EINTR(::read(fd_, addr, bytes - total_read));
      if (bytes_read <= 0) {
        break;
      }
      total_read += bytes_read;
    }
    return total_read == bytes;
  }

 private:
  // friend class muduo::Singleton<URandom>;

  URandom() : fd_(HANDLE_EINTR(::open("/dev/urandom", O_RDONLY | O_CLOEXEC))) {
    if (fd_ < 0) {
      LOG_SYSFATAL << "open /dev/urandom failed";
    }
  }

  ~URandom() { ignore_result(::close(fd_)); }

  const int fd_{-1};

  DISALLOW_COPY_AND_ASSIGN(URandom);
};

#endif
