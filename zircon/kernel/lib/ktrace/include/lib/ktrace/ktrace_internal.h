// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_KTRACE_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_KTRACE_INTERNAL_H_

#include <assert.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/function.h>
#include <ktl/atomic.h>
#include <ktl/forward.h>

namespace internal {

class KTraceState {
 public:
  constexpr KTraceState() = default;

  // Initialize the KTraceState instance, may only be called once.  Any methods
  // called on a KTraceState instance after construction, but before Init,
  // should behave as no-ops.
  //
  // |target_bufsize| : The target size (in bytes) of the ktrace buffer to be
  // allocated.  Must be a multiple of 8 bytes.
  //
  // |initial_groups| : The initial set of enabled trace groups (see
  // zircon-internal/ktrace.h).  If non-zero, causes Init to attempt to allocate
  // the trace buffer immediately.  If the allocation fails, or the initial
  // group mask is zero, allocation is delayed until the first time that start
  // is called.
  //
  void Init(uint32_t target_bufsize, uint32_t initial_groups);

  zx_status_t Start(uint32_t groups);
  void Stop();
  void Rewind();
  ssize_t ReadUser(void* ptr, uint32_t off, size_t len);

  // Write a record to the tracelog.
  //
  // |payload| must consist of all uint32_t or all uint64_t types.
  template <typename... Args>
  void WriteRecord(uint32_t effective_tag, uint64_t explicit_ts, Args... args);
  void WriteRecordTiny(uint32_t tag, uint32_t arg);
  void WriteNameEtc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always);

  // Determine if ktrace is enabled for the given tag.
  inline bool tag_enabled(uint32_t tag) const {
    return (grpmask_.load(std::memory_order_relaxed) & tag) != 0;
  }

 protected:
  // Add static names (eg syscalls and probes) to the trace buffer.  Called
  // during a rewind operation immediately after resetting the trace buffer.
  // Declared as virtual to facilitate testing.
  virtual void ReportStaticNames();

  // Add the names of current live threads and processes to the trace buffer.
  // Called during start operations just before setting the group mask. Declared
  // as virtual to facilitate testing.
  virtual void ReportThreadProcessNames();

  // A small printf stand-in which gives tests the ability to disable diagnostic
  // printing during testing.
  int DiagsPrintf(int level, const char* fmt, ...) __PRINTFLIKE(3, 4) {
    if (!disable_diags_printfs_ && DPRINTF_ENABLED_FOR_LEVEL(level)) {
      va_list args;
      va_start(args, fmt);
      int result = vprintf(fmt, args);
      va_end(args);
      return result;
    }

    return 0;
  }

  // raw trace buffer
  // if this is nullptr, then bufsize == 0
  //
  // Protected so that the test fixture can free it.  The default global
  // singleton KTraceState never frees its buffer.
  uint8_t* buffer_{nullptr};

  // Allow diagnostic dprintf'ing or not.  Overridden by test code.
  bool disable_diags_printfs_{false};

 private:
  // Attempt to allocate our buffer, if we have not already done so.
  zx_status_t AllocBuffer();

  // Allocates a new trace record in the trace buffer. Returns a pointer to the
  // start of the record or nullptr the end of the buffer is reached.
  void* Open(uint32_t tag, uint64_t ts);

  inline void Disable() {
    grpmask_.store(0);
    buffer_full_.store(true);
  }

  // Mask of trace groups that should be traced. If 0, then all tracing is
  // disabled.
  //
  // This value is frequently read and rarely modified.
  ktl::atomic<uint32_t> grpmask_{0};

  // The target buffer size (in bytes) we would like to use, when we eventually
  // call AllocBuffer.  Set during the call to Init.
  uint32_t target_bufsize_{0};

  // where the next record will be written
  ktl::atomic<uint32_t> offset_{0};

  // Total size of the trace buffer
  uint32_t bufsize_{0};

  // offset where tracing was stopped, 0 if tracing active
  uint32_t marker_{0};

  // buffer is full or not
  ktl::atomic<bool> buffer_full_{false};
};

}  // namespace internal

#endif  // ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_KTRACE_INTERNAL_H_
