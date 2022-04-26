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

// Contains an array of return address values representing a thread's backtrace.
//
// This class is not thread-safe.
class Backtrace {
 public:
  // The maximum number of elements in a backtrace.  The value should be large enough to capture the
  // full call stack for most kernel crashes, but also small enough that it's reasonable to allocate
  // a Backtrace on the stack.
  static constexpr size_t kMaxSize = 32;

  // Returns the number of elements in this backtrace.
  size_t size() const { return size_; }

  // Returns a pointer to the underlying elements.
  const vaddr_t* data() const { return addr_; }

  // Resets the size to 0.
  void reset() { size_ = 0; }

  // Adds one element to the array.  See also |set_first_frame_type|.
  void push_back(vaddr_t addr) {
    if (size_ < kMaxSize) {
      addr_[size_++] = addr;
    }
  }

  // With the possible exception of the first frame, each address in the backtrace is a return
  // address.  The first frame may be either a return address or a precise location (think PC).  See
  // the "Presentation elements" section of //docs/reference/kernel/symbolizer_markup.md for
  // details.  Unless specified, the first frame is assumed to be a return address.
  enum FrameType { ReturnAddress, PreciseLocation };
  void set_first_frame_type(FrameType type) { first_frame_type_ = type; }

  // Pretty-prints this backtrace to |file|.
  void Print(FILE* = stdout) const;
  void PrintWithoutVersion(FILE* = stdout) const;

 private:
  vaddr_t addr_[kMaxSize]{};
  size_t size_{};
  FrameType first_frame_type_{ReturnAddress};
};

#endif  // ZIRCON_KERNEL_LIB_BACKTRACE_INCLUDE_LIB_BACKTRACE_H_
