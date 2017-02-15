// Taken from Chromium stack_trace_posix.h
// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//   * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//   * Neither the name of Shuo Chen nor the names of other contributors
// may be used to endorse or promote products derived from this software
// without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef COMMON_DEBUG_STACK_TRACE_H_
#define COMMON_DEBUG_STACK_TRACE_H_

#include <stdint.h>

#include <string>

#include "common/macros.h"

// stacktrace member in a object (probably around #ifndef NDEBUG) so that you
// can later see where the given object was created from.
class DLL_PUBLIC StackTrace {
 public:
  // Creates a stacktrace from the current location.
  StackTrace();

  // Creates a stacktrace from an existing array of instruction
  // pointers (such as returned by Addresses()).  |count| will be
  // trimmed to |kMaxTraces|.
  StackTrace(const void* const* trace, size_t count);

  // Copying and assignment are allowed with the default functions.

  ~StackTrace();

  // Gets an array of instruction pointer values. |*count| will be set to the
  // number of elements in the returned array.
  // const void* const* Addresses(size_t* count) const;

  // Prints the stack trace to stderr.
  void Print() const;

  // Resolves backtrace to symbols and returns as string.
  std::string ToString() const;

 private:
  // From http://msdn.microsoft.com/en-us/library/bb204633.aspx,
  // the sum of FramesToSkip and FramesToCapture must be less than 63,
  // so set it to 62. Even if on POSIX it could be a larger value, it usually
  // doesn't give much more information.
  static const int kMaxTraces = 62;

  void* trace_[kMaxTraces];

  // The number of valid frames in |trace_|.
  size_t count_;
};

DLL_PUBLIC char* itoa_r(intptr_t i, char* buf, size_t sz, int base,
                        size_t padding);

template <typename T>
class DLL_PUBLIC AutoFree;

DLL_PUBLIC void PrintToStderr(const char* output);

#endif  // COMMON_DEBUG_STACK_TRACE_H_
