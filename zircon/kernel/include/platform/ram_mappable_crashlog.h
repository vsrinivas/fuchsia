// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_RAM_MAPPABLE_CRASHLOG_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_RAM_MAPPABLE_CRASHLOG_H_

#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <ktl/atomic.h>
#include <ktl/span.h>
#include <platform/crashlog.h>
#include <ram-crashlog/ram-crashlog.h>

class RamMappableCrashlog final : public PlatformCrashlog::Interface {
 public:
  RamMappableCrashlog() = delete;
  RamMappableCrashlog(paddr_t phys, size_t len);

  ktl::span<char> GetRenderTarget() final { return {render_target_, sizeof(render_target_)}; }

  void Finalize(zircon_crash_reason_t reason, size_t amt) final;
  size_t Recover(FILE* tgt) final;
  void EnableCrashlogUptimeUpdates(bool enabled) final;

 private:
  bool ShouldPrintCrashlogStatus() {
    bool expected = false;
    return status_printed_to_debuglog_.compare_exchange_strong(expected, true,
                                                               ktl::memory_order_relaxed);
  }

  void UpdateUptimeLocked() TA_REQ(uptime_updater_lock_);

  const ktl::span<char> crashlog_buffer_;

  char render_target_[4096];
  recovered_ram_crashlog_t recovered_log_;
  zx_status_t log_recovery_result_ = ZX_ERR_INTERNAL;

  DECLARE_SPINLOCK(RamMappableCrashlog) uptime_updater_lock_;
  Timer uptime_updater_timer_ TA_GUARDED(uptime_updater_lock_);
  bool uptime_updater_enabled_ TA_GUARDED(uptime_updater_lock_) = false;

  // Make sure we print the crashlog status to the debuglog only once, no matter
  // how many times recover_crashlog is called.
  ktl::atomic<bool> status_printed_to_debuglog_{false};
};

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_RAM_MAPPABLE_CRASHLOG_H_
