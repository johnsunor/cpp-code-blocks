
#ifndef LOG_UTIL_H_
#define LOG_UTIL_H_

// for test
#define ASSERT_EXIT(x)                           \
  ({                                             \
    auto ok = (x);                               \
    if (!ok) {                                   \
      LOG_ERROR << "assert " << #x << " failed"; \
      ::exit(0);                                 \
    }                                            \
  })

// for test
#define ERROR_EXIT(x)                                            \
  ({                                                             \
    auto rv = (x);                                               \
    if (rv < 0) {                                                \
      LOG_ERROR << #x << " failed: " << muduo::strerror_tl(-rv); \
      ::exit(0);                                                 \
    }                                                            \
    rv;                                                          \
  })

// for UDPSocket
#define ERROR_RETURN(x)       \
  ({                          \
    auto rv = (x);            \
    if (rv < 0) {             \
      int last_error = errno; \
      LOG_SYSERR << #x;       \
      return -last_error;     \
    }                         \
    rv;                       \
  })

#endif
