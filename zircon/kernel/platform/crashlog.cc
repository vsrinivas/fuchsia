// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>

#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <ktl/limits.h>
#include <platform/crashlog.h>
#include <ram-crashlog/ram-crashlog.h>
#include <vm/physmap.h>

namespace {

void* ram_crashlog_vaddr;
size_t ram_crashlog_size;
recovered_ram_crashlog_t recovered_log;
zx_status_t log_recovery_result = ZX_ERR_INTERNAL;

DECLARE_SINGLETON_SPINLOCK(uptime_updater_lock);
Timer uptime_updater_timer TA_GUARDED(uptime_updater_lock::Get());
bool uptime_updater_enabled TA_GUARDED(uptime_updater_lock::Get()) = false;

void default_platform_stow_crashlog(zircon_crash_reason_t reason, const void* log, size_t len) {
  // We are not going to store more than 4GB of payload.  That is just not happening.
  if (len > ktl::numeric_limits<uint32_t>::max()) {
    len = ktl::numeric_limits<uint32_t>::max();
  }

  // The RAM crashlog library will gracefully handle a nullptr or 0 length here;
  // no need to explicitly check that they are valid.
  ram_crashlog_stow(ram_crashlog_vaddr, ram_crashlog_size, log, static_cast<uint32_t>(len), reason,
                    current_time());
}

size_t default_platform_recover_crashlog(size_t len, void* cookie,
                                         void (*func)(const void* data, size_t off, size_t len,
                                                      void* cookie)) {
  // If we failed to recover any crashlog, simply report the size as 0. This is most likely a cold
  // boot.
  if (log_recovery_result != ZX_OK) {
    return 0;
  }

  // OK, we have a log.  Render the "preamble" of the log into a local stack
  // buffer as part of computing the final size.  Currently, the log is expected
  // to be nothing but text, so we need to take the structured information we
  // have access to and put it into string form.  This includes:
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
  const recovered_ram_crashlog_t& rlog = recovered_log;
  ZbiHwRebootReason hw_reason = platform_hw_reboot_reason();
  const char* str_reason;
  char preamble[256];
  size_t offset = 0;
  switch (rlog.reason) {
    case ZirconCrashReason::Unknown:
      // If we rebooted spontaneously, check to see if we have some more details
      // provided by way of the bootloader and the HW reboot reason register.
      switch (hw_reason) {
        case ZbiHwRebootReason::Brownout:
          str_reason = "BROWNOUT";
          break;
        case ZbiHwRebootReason::Watchdog:
          str_reason = "HW WATCHDOG";
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
    default:
      str_reason = nullptr;
      break;
  }

  // First line must give the reboot reason, and be followed by two newlines.
  DEBUG_ASSERT(offset <= sizeof(preamble));
  if (str_reason != nullptr) {
    offset += snprintf(preamble + offset, sizeof(preamble) - offset,
                       "ZIRCON REBOOT REASON (%s)\n\n", str_reason);
  } else {
    offset += snprintf(preamble + offset, sizeof(preamble) - offset,
                       "ZIRCON REBOOT REASON (0x%08x)\n\n", static_cast<uint32_t>(rlog.reason));
  }

  // Uptime estimate comes next with a newline between the tag and the actual number
  DEBUG_ASSERT(offset <= sizeof(preamble));
  offset += snprintf(preamble + offset, sizeof(preamble) - offset, "UPTIME (ms)\n%ld\n",
                     rlog.uptime / ZX_MSEC(1));

  // After this, we are basically just free form text.
  const char* str_hw_reason;
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
      str_hw_reason = "WATCHDOG";
      break;
    default:
      str_hw_reason = nullptr;
      break;
  }

  DEBUG_ASSERT(offset <= sizeof(preamble));
  if (str_hw_reason != nullptr) {
    offset += snprintf(preamble + offset, sizeof(preamble) - offset, "HW REBOOT REASON (%s)\n",
                       str_hw_reason);
  } else {
    offset += snprintf(preamble + offset, sizeof(preamble) - offset, "HW REBOOT REASON (0x%08x)\n",
                       static_cast<uint32_t>(hw_reason));
  }

  if (rlog.payload_valid == false) {
    DEBUG_ASSERT(offset <= sizeof(preamble));
    offset +=
        snprintf(preamble + offset, sizeof(preamble) - offset,
                 "WARNING - The following crashlog payload failed length/CRC sanity checks and may "
                 "contain errors!\n");
  }

  // If the user passed us a length of zero, then they just want us to tell them
  // the size of a rendered log.  Don't make any callbacks if this is the case.
  if (len != 0) {
    DEBUG_ASSERT(offset <= sizeof(preamble));
    func(preamble, 0, offset, cookie);
    if (rlog.payload && rlog.payload_len) {
      func(rlog.payload, offset, rlog.payload_len, cookie);
    }
  }

  // Report the total length.
  return offset + rlog.payload_len;
}

void update_uptime_locked() TA_REQ(uptime_updater_lock::Get()) {
  if (uptime_updater_enabled) {
    constexpr zx_duration_t kDefaultUpdateInterval = ZX_SEC(1);

    default_platform_stow_crashlog(ZirconCrashReason::Unknown, nullptr, 0);

    Deadline next_update_time =
        Deadline::after(kDefaultUpdateInterval, {kDefaultUpdateInterval / 2, TIMER_SLACK_CENTER});
    uptime_updater_timer.Set(
        next_update_time,
        [](Timer*, zx_time_t now, void* arg) {
          Guard<SpinLock, IrqSave> guard{uptime_updater_lock::Get()};
          update_uptime_locked();
        },
        nullptr);
  }
}

void default_platform_enable_crashlog_uptime_updates(bool enabled) {
  // Can't enable something we don't have.
  enabled = enabled && platform_has_ram_crashlog();
  {
    Guard<SpinLock, IrqSave> guard{uptime_updater_lock::Get()};

    if (uptime_updater_enabled != enabled) {
      uptime_updater_enabled = enabled;
      if (uptime_updater_enabled) {
        update_uptime_locked();
      } else {
        uptime_updater_timer.Cancel();
      }
    }
  }
}

}  // namespace

void (*platform_stow_crashlog)(zircon_crash_reason_t reason, const void* log,
                               size_t len) = default_platform_stow_crashlog;
size_t (*platform_recover_crashlog)(size_t len, void* cookie,
                                    void (*func)(const void* data, size_t off, size_t len,
                                                 void* cookie)) = default_platform_recover_crashlog;
void (*platform_enable_crashlog_uptime_updates)(bool enabled) =
    default_platform_enable_crashlog_uptime_updates;

void platform_set_ram_crashlog_location(paddr_t phys, size_t len) {
  if (phys && len) {
    ram_crashlog_vaddr = paddr_to_physmap(phys);
    ram_crashlog_size = len;

    // Go ahead and "recover" the log right now.  All this will do is verify the
    // various CRCs and extract the results if everything checks out.  We don't
    // want to do this more than once.
    log_recovery_result =
        ram_crashlog_recover(ram_crashlog_vaddr, ram_crashlog_size, &recovered_log);
  }
}

bool platform_has_ram_crashlog() { return ram_crashlog_vaddr && ram_crashlog_size; }
