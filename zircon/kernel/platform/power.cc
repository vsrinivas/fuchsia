// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <lib/boot-options/boot-options.h>
#include <lib/console.h>
#include <lib/crashlog.h>
#include <lib/debuglog.h>
#include <lib/jtrace/jtrace.h>
#include <lib/persistent-debuglog.h>
#include <platform.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>

#include <dev/hw_watchdog.h>
#include <kernel/thread.h>
#include <platform/crashlog.h>
#include <platform/debug.h>

// Common platform halt path.  This handles some tasks we always want to make
// sure we handle before dropping into the common platform specific halt
// routine.
void platform_halt(platform_halt_action suggested_action, zircon_crash_reason_t reason) {
  // Disable the automatic uptime updating.  We are going to attempt to
  // deliberately halt the system, and we don't want the crashlog to indicate a
  // spontaneous reboot.
  PlatformCrashlog::Get().EnableCrashlogUptimeUpdates(false);

  // We are haling on purpose.  Disable the watchdog (if we have one, and if we
  // can) if we plan to halt instead of instigate a reboot.  If we are going to
  // try to actually reboot, pet the dog one last time to give ourselves the
  // maximum amount of time to arrange our graceful reboot.
  bool halt_on_panic = gBootOptions->halt_on_panic;
  if (ENABLE_PANIC_SHELL || halt_on_panic) {
    hw_watchdog_set_enabled(false);
  } else {
    hw_watchdog_pet();
  }

  // Was this an OOM, panic, or software watchdog condition?  If so, and we have
  // space to render a crashlog into, render the payload of our crashlog before
  // stowing our reason.  Then, whether we have a payload or not, stow our final
  // crashlog.
  if ((reason == ZirconCrashReason::Oom) || (reason == ZirconCrashReason::Panic) ||
      (reason == ZirconCrashReason::SoftwareWatchdog)) {
    auto& crashlog = PlatformCrashlog::Get();
    size_t rendered_crashlog_len = 0;

    if (ktl::span<char> target = crashlog.GetRenderTarget(); target.size() > 0) {
      rendered_crashlog_len = crashlog_to_string(target, reason);
    }

    crashlog.Finalize(reason, rendered_crashlog_len);
  }

  // This is a graceful reboot, invalidate the persistent dlog (if we have one)
  // and persistent trace buffer (if we have one) so that we don't attempt to
  // recover it during a reboot.
  if (reason == ZirconCrashReason::NoCrash) {
    persistent_dlog_invalidate();
    jtrace_invalidate();
  }

  // Finally, fall into the platform specific halt handler.
  platform_specific_halt(suggested_action, reason, halt_on_panic);
}

namespace {
__NO_RETURN
int cmd_reboot(int argc, const cmd_args* argv, uint32_t flags) {
  bool is_panic_shell = (flags & CMD_FLAG_PANIC) != 0;

  // If we are already panicking, don't repeat the first half of `platform_halt`. Instead,
  // just finish the reboot.
  if (is_panic_shell) {
    platform_specific_halt(HALT_ACTION_REBOOT, ZirconCrashReason::Panic,
                           /*halt_on_panic=*/false);
    // unreachable
  }

  platform_halt(HALT_ACTION_REBOOT, ZirconCrashReason::NoCrash);
}
}  // namespace

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("reboot", "reboot the system", &cmd_reboot, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(power)
