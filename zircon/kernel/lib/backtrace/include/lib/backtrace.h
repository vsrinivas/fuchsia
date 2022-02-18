// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_BACKTRACE_INCLUDE_LIB_BACKTRACE_H_
#define ZIRCON_KERNEL_LIB_BACKTRACE_INCLUDE_LIB_BACKTRACE_H_

#include <assert.h>
#include <sys/types.h>
#include <zircon/types.h>

// Contains an array of PC values representing a thread's backtrace.
//
// This class is not thread-safe.
class Backtrace {
 public:
  // The maximum number of elements in a backtrace.
  static constexpr size_t kMaxSize = 16;

  // Returns the number of elements in this backtrace.
  size_t size() const { return size_; }

  // Returns a pointer to the underlying elements.
  const vaddr_t* data() const { return pc_; }

  // Resets the size to 0.
  void reset() { size_ = 0; }

  // Adds one element to the array.
  void push_back(vaddr_t pc) {
    if (size_ < kMaxSize) {
      pc_[size_++] = pc;
    }
  }

  // Pretty-prints this backtrace to |file|.
  void Print(FILE* = stdout) const;
  void PrintWithoutVersion(FILE* = stdout) const;

 private:
  vaddr_t pc_[kMaxSize]{};
  size_t size_{};
};

#endif  // ZIRCON_KERNEL_LIB_BACKTRACE_INCLUDE_LIB_BACKTRACE_H_
