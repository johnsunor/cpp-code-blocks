
#ifndef COMMON_MACROS_H_
#define COMMON_MACROS_H_

#if !defined(__GNUC__) || !defined(__cplusplus)
#error Please use g++ to compile
#endif

#include <stddef.h>  // For size_t.
#include <string.h>  // For memcpy.

#include <errno.h>  // For errno.

#include <features.h>  // For __GNUC_PREREQ

#if defined(_M_X64) || defined(__x86_64__)
#define ARCH_CPU_X86_FAMILY 1
#define ARCH_CPU_X86_64 1
#define ARCH_CPU_64_BITS 1
#define ARCH_CPU_LITTLE_ENDIAN 1
#define ARCH_WORD_BIT 16
#elif defined(_M_IX86) || defined(__i386__)
#define ARCH_CPU_X86_FAMILY 1
#define ARCH_CPU_X86 1
#define ARCH_CPU_32_BITS 1
#define ARCH_CPU_LITTLE_ENDIAN 1
#define ARCH_WORD_BIT 32
#endif

// https://gcc.gnu.org/onlinedocs/gcc/C-Extensions.html#C-Extensions
// https://gcc.gnu.org/onlinedocs/gcc/Alternate-Keywords.html#Alternate-Keywords
// The keywords asm, typeof and inline are not available in programs compiled
// with -ansi or -std.
#define asm __asm__
#define typeof __typeof__
//#define inline __inline__
//#define volatile __volatile__

#define ALLOW_UNUSED __attribute__((unused))
#define UNUSED(v) (void)(v)

template <bool>
struct CompileAssert {};

#define COMPILE_ASSERT(expr, msg) \
  typedef CompileAssert<(bool(expr))> msg[bool(expr) ? 1 : -1] ALLOW_UNUSED

template <typename T>
inline void ignore_result(const T&) {}

template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];

// safe arraysize
#define arraysize(array) (sizeof(ArraySizeHelper(array)))

#if defined(NDEBUG)

#define HANDLE_EINTR(x)                                     \
  ({                                                        \
    decltype(x) eintr_wrapper_result;                       \
    do {                                                    \
      eintr_wrapper_result = (x);                           \
    } while (eintr_wrapper_result == -1 && errno == EINTR); \
    eintr_wrapper_result;                                   \
  })

#else

#define HANDLE_EINTR(x)                                      \
  ({                                                         \
    int eintr_wrapper_counter = 0;                           \
    decltype(x) eintr_wrapper_result;                        \
    do {                                                     \
      eintr_wrapper_result = (x);                            \
    } while (eintr_wrapper_result == -1 && errno == EINTR && \
             eintr_wrapper_counter++ < 100);                 \
    eintr_wrapper_result;                                    \
  })

#endif  // NDEBUG

#define IGNORE_EINTR(x)                                   \
  ({                                                      \
    decltype(x) eintr_wrapper_result;                     \
    do {                                                  \
      eintr_wrapper_result = (x);                         \
      if (eintr_wrapper_result == -1 && errno == EINTR) { \
        eintr_wrapper_result = 0;                         \
      }                                                   \
    } while (0);                                          \
    eintr_wrapper_result;                                 \
  })

#define ALIGNOF(type) __alignof__(type)

// https://gcc.gnu.org/onlinedocs/gcc-5.1.0/gcc/Type-Attributes.html
// The aligned attribute can only increase the alignment;
// but you can decrease it by specifying packed as well.
#define ALIGNAS(byte_alignment) __attribute__((aligned(byte_alignment)))
#define PACKED __attribute__((packed))

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// https://gcc.gnu.org/wiki/Visibility
// If __GNUC_PREREQ(4, 0) is true, then it means the compiler is GCC
// version 4.0 or later, and hence supports the new features.
#if __GNUC_PREREQ(4, 0)
#define DLL_PUBLIC __attribute__((visibility("default")))
#define DLL_LOCAL __attribute__((visibility("hidden")))
#else
#define DLL_PUBLIC
#define DLL_LOCAL
#endif

// All the error names specified by POSIX.1 must have distinct values,
// with the exception of EAGAIN and EWOULDBLOCK, which may be the same.
#define IS_EAGAIN(error) \
  (((error) == EAGAIN || (error) == EWOULDBLOCK) ? true : false)

// GCC 4.7 supports explicit virtual overrides when C++11 support is
// enabled.
#if __GNUC_PREREQ(4, 7) && __cplusplus >= 201103L
#define OVERRIDE override
#define FINAL final

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete

#else
#define OVERRIDE
#define FINAL

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

#endif

#define NOINLINE __attribute__((noinline))

#endif  // COMMON_MACROS_H_
