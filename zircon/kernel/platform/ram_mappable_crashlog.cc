// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/persistent-debuglog.h>
#include <stdio.h>

#include <kernel/timer.h>
#include <ktl/algorithm.h>
#include <ktl/limits.h>
#include <ktl/span.h>
#include <platform/ram_mappable_crashlog.h>
#include <ram-crashlog/ram-crashlog.h>
#include <vm/physmap.h>

namespace {
FILE NULL_FILE = FILE{[](void*, ktl::string_view str) -> int {
                        DEBUG_ASSERT(str.size() <= ktl::numeric_limits<int>::max());
                        return static_cast<int>(str.size());
                      },
                      nullptr};
}

RamMappableCrashlog::RamMappableCrashlog(paddr_t phys, size_t len)
    : crashlog_buffer_(phys && len
                           ? ktl::span<char>{static_cast<char*>(paddr_to_physmap(phys)), len}
                           : ktl::span<char>{}),
      render_target_(crashlog_buffer_.size() > sizeof(ram_crashlog_t)
                         ? crashlog_buffer_.subspan(sizeof(ram_crashlog_t))
                         : ktl::span<char>{}) {
  if (!crashlog_buffer_.empty()) {
    // Go ahead and "recover" the log right now.  All this will do is verify the
    // various CRCs and extract the results if everything checks out.  We don't
    // want to do this more than once.
    log_recovery_result_ =
        ram_crashlog_recover(crashlog_buffer_.data(), crashlog_buffer_.size(), &recovered_log_);
  } else {
    memset(&recovered_log_, 0, sizeof(recovered_log_));
  }
}

void RamMappableCrashlog::Finalize(zircon_crash_reason_t reason, size_t amt) {
  // Whatever the user tells us, the amt of the crashlog render target which was
  // filled cannot exceed the amount that we originally reported, nor can it be
  // larger than what a u32 can hold.
  amt = ktl::min(amt, render_target_.size());
  amt = ktl::min<size_t>(amt, ktl::numeric_limits<uint32_t>::max());

  // The RAM crashlog library will gracefully handle a nullptr or 0 length here;
  // no need to explicitly check that they are valid.
  ram_crashlog_stow(crashlog_buffer_.data(), crashlog_buffer_.size(), render_target_.data(),
                    static_cast<uint32_t>(amt), reason, current_time());
}

size_t RamMappableCrashlog::Recover(FILE* tgt) {
  // If the user didn't supply a target FILE to render to, use the NULL_FILE
  // instead so that we compute a proper length for them as we go.
  if (tgt == nullptr) {
    tgt = &NULL_FILE;
  }

  // Create a string representation of the HW reboot reason.
  ZbiHwRebootReason hw_reason = platform_hw_reboot_reason();
  const char* str_hw_reason;
  char str_hw_reason_buf[16];
  switch (hw_reason) {
    case ZbiHwRebootReason::Undefined:
      str_hw_reason = "UNKNOWN";
      break;
    case ZbiHwRebootReason::Cold:
      str_hw_reason = "COLD BOOT";
      break;
    case ZbiHwRebootReason::Warm:
      str_hw_reason = "WARM BOOT";
      break;
    case ZbiHwRebootReason::Brownout:
      str_hw_reason = "BROWNOUT";
      break;
    case ZbiHwRebootReason::Watchdog:
      str_hw_reason = "HW WATCHDOG";
      break;
    default:
      snprintf(str_hw_reason_buf, sizeof(str_hw_reason_buf), "0x%08x",
               static_cast<uint32_t>(hw_reason));
      str_hw_reason = str_hw_reason_buf;
      break;
  }

  // If we failed to recover any crashlog, simply report the size as 0.
  if (log_recovery_result_ != ZX_OK) {
    // Do not bother to log any recovery errors if the log was "corrupt", and we
    // either don't know the HW reboot reason, or we know that the reason is a
    // cold boot.  We don't expect to recover any log during a cold boot, and
    // systems which do not report a HW reboot reason via the ZBI will always
    // just tell us "unknown".
    if (ShouldPrintCrashlogStatus()) {
      if (!((log_recovery_result_ == ZX_ERR_IO_DATA_INTEGRITY) &&
            ((hw_reason == ZbiHwRebootReason::Undefined) ||
             (hw_reason == ZbiHwRebootReason::Cold)))) {
        printf("Crashlog: Failed to recover crashlog.  Result %d, HW Reboot Reason %s\n",
               log_recovery_result_, str_hw_reason);
      }
    }

    return 0;
  }

  // OK, we have a log.  Currently, the log is expected to be nothing but text,
  // so we need to take the structured information we have access to and put it
  // into string form.  This includes:
  //
  // 1) The uptime estimate
  // 2) The "software" reboot reason.
  // 3) The "hardware" reboot reason (only if given to us by the bootloader).
  // 4) The payload damage indicator (only if there was potential damage to the
  //    payload)
  //
  // The first few lines of text need to be structured so that they can be
  // understood by the crash-log harvester up in userland.  Right now, this is
  // just a loose convention.  Someday, it would be good to pass this data in a
  // much more structured form.
  const recovered_ram_crashlog_t& rlog = recovered_log_;
  const char* str_reason;
  switch (rlog.reason) {
    case ZirconCrashReason::Unknown:
      // If we rebooted spontaneously, check to see if we have some more details
      // provided by way of the bootloader and the HW reboot reason register.
      switch (hw_reason) {
        case ZbiHwRebootReason::Brownout:
        case ZbiHwRebootReason::Watchdog:
          str_reason = str_hw_reason;
          break;
        default:
          str_reason = "UNKNOWN";
          break;
      }
      break;
    case ZirconCrashReason::Oom:
      str_reason = "OOM";
      break;
    case ZirconCrashReason::Panic:
      str_reason = "KERNEL PANIC";
      break;
    case ZirconCrashReason::SoftwareWatchdog:
      str_reason = "SW WATCHDOG";
      break;
    case ZirconCrashReason::NoCrash:
      str_reason = "NO CRASH";
      break;
    case ZirconCrashReason::UserspaceRootJobTermination:
      str_reason = "USERSPACE ROOT JOB TERMINATION";
      break;
    default:
      str_reason = nullptr;
      break;
  }

  if (ShouldPrintCrashlogStatus()) {
    // Provide some basic details about the crashlog we recovered in the kernel
    // log.  This can assist in debugging failure in CI/CQ where we might have
    // access to serial logs, but nothing else.
    int64_t uptime_msec = rlog.uptime / ZX_MSEC(1);
    if (rlog.reason == ZirconCrashReason::NoCrash) {
      printf("Crashlog: Clean reboot. Uptime (%" PRId64 ".%03" PRId64 " sec) HW Reason \"%s\"\n",
             uptime_msec / 1000, uptime_msec % 1000, str_hw_reason);
    } else {
      printf("Crashlog: Uptime (%" PRId64 ".%03" PRId64
             " sec) SW Reason \"%s\" HW Reason \"%s\" Payload %s PLen %u\n",
             uptime_msec / 1000, uptime_msec % 1000, str_reason, str_hw_reason,
             rlog.payload_valid ? "valid" : "invalid", rlog.payload_len);
    }
  }

  // First line must give the reboot reason, and be followed by two newlines.
  size_t written = 0;
  written += fprintf(tgt, "ZIRCON REBOOT REASON (%s)\n\n", str_reason);

  // Uptime estimate comes next with a newline between the tag and the actual number
  written += fprintf(tgt, "UPTIME (ms)\n%ld\n", rlog.uptime / ZX_MSEC(1));

  // After this, we are basically just free form text.
  if (str_hw_reason != nullptr) {
    written += fprintf(tgt, "HW REBOOT REASON (%s)\n", str_hw_reason);
  } else {
    written += fprintf(tgt, "HW REBOOT REASON (0x%08x)\n", static_cast<uint32_t>(hw_reason));
  }

  if (rlog.payload_valid == false) {
    written += fprintf(tgt,
                       "WARNING - The following crashlog payload failed length/CRC sanity checks "
                       "and may contain errors!\n");
  }

  if (rlog.payload && rlog.payload_len) {
    tgt->Write(ktl::string_view{static_cast<const char*>(rlog.payload), rlog.payload_len});
    written += rlog.payload_len;
  }

  // Render any persistent dlog we happened to recover
  ktl::string_view dlog = persistent_dlog_get_recovered_log();
  if (dlog.size() > 0) {
    int print_res =
        fprintf(tgt, "Recovered %zu bytes from the persistent debug log\n", dlog.size());
    if (print_res > 0) {
      written += print_res;
    }

    print_res = fprintf(tgt, "=================== BEGIN ===================\n");
    if (print_res > 0) {
      written += print_res;
    }

    written += tgt->Write(dlog);

    print_res = fprintf(tgt, "=================== END ===================\n");
    if (print_res > 0) {
      written += print_res;
    }
  }

  // Report the total length.
  return written + rlog.payload_len;
}

void RamMappableCrashlog::EnableCrashlogUptimeUpdates(bool enabled) {
  Guard<SpinLock, IrqSave> guard{&uptime_updater_lock_};

  if (uptime_updater_enabled_ != enabled) {
    uptime_updater_enabled_ = enabled;
    if (uptime_updater_enabled_) {
      UpdateUptimeLocked();
    } else {
      uptime_updater_timer_.Cancel();
    }
  }
}

void RamMappableCrashlog::UpdateUptimeLocked() {
  if (uptime_updater_enabled_) {
    constexpr zx_duration_t kDefaultUpdateInterval = ZX_SEC(1);

    ram_crashlog_stow(crashlog_buffer_.data(), crashlog_buffer_.size(), nullptr, 0,
                      ZirconCrashReason::Unknown, current_time());

    Deadline next_update_time =
        Deadline::after(kDefaultUpdateInterval, {kDefaultUpdateInterval / 2, TIMER_SLACK_CENTER});
    uptime_updater_timer_.Set(
        next_update_time,
        [](Timer*, zx_time_t now, void* arg) {
          auto thiz = reinterpret_cast<RamMappableCrashlog*>(arg);
          Guard<SpinLock, IrqSave> guard{&thiz->uptime_updater_lock_};
          thiz->UpdateUptimeLocked();
        },
        this);
  }
}
