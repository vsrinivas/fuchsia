// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CRASHLOG_INCLUDE_LIB_CRASHLOG_PANIC_BUFFER_H_
#define ZIRCON_KERNEL_LIB_CRASHLOG_INCLUDE_LIB_CRASHLOG_PANIC_BUFFER_H_

#include <kernel/lockdep.h>
#include <kernel/spinlock.h>

// A fixed-size, NUL-terminated buffer for storing formatted panic/assert messages.
//
// While this buffer is safe for concurrent use by multiple threads, concurrent use may result in
// message loss or (logical) message corruption.
class PanicBuffer {
 public:
  PanicBuffer() = default;
  PanicBuffer(const PanicBuffer&) = delete;
  PanicBuffer& operator=(const PanicBuffer&) = delete;
  PanicBuffer(PanicBuffer&&) = delete;
  PanicBuffer& operator=(PanicBuffer&&) = delete;

  // This value should be small enough so that an instance of this class can fit on the stack, but
  // large enough to capture the last few lines emitted by an assert failure or panic to ensure any
  // formatted message is captured for later analysis.
  //
  // Note, increasing this size will not necessarily ensure that more of the assert/panic message is
  // captured.  There are other limits in place that may truncate the message.  See
  // |crashlog_to_string|.
  static constexpr size_t kMaxSize = 2048;

  // Append |s| to the buffer.
  //
  // Silently drops data if the buffer is full.
  //
  // TODO(maniscalco): Given that the formatted message arguments are more valuable than the format
  // string, consider giving this class circular buffer semantics.
  void Append(ktl::string_view s);

  // Returns a pointer to a NUL-terminated C string.
  const char* c_str() const { return buffer_; }

 private:
  DECLARE_SPINLOCK(PanicBuffer) lock_;
  size_t pos_ TA_GUARDED(lock_) = 0;
  char buffer_[kMaxSize] TA_GUARDED(lock_) = {};
};

#endif  // ZIRCON_KERNEL_LIB_CRASHLOG_INCLUDE_LIB_CRASHLOG_PANIC_BUFFER_H_
