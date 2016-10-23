
#ifndef HELP_UTILS_H_
#define HELP_UTILS_H_

#include <stddef.h>  // For size_t.
#include <string.h>  // For memcpy.

#define ALLOW_UNUSED __attribute__((unused))

#define COMPILE_ASSERT(expr, msg) \
  typedef CompileAssert<(bool(expr))> msg[bool(expr) ? 1 : -1] ALLOW_UNUSED

template<typename T>
inline void ignore_result(const T&) {
}

template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];

#define arraysize(array) (sizeof(ArraySizeHelper(array)))

#endif

