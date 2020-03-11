// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <err.h>
#include <lib/cmdline.h>
#include <lib/console.h>
#include <lib/crashlog.h>
#include <lib/debuglog.h>
#include <platform.h>
#include <stdio.h>
#include <zircon/compiler.h>

#include <dev/hw_watchdog.h>
#include <kernel/thread.h>
#include <platform/crashlog.h>
#include <platform/debug.h>

namespace {
char crashlog_render_buffer[4096u];
}

// Common platform halt path.  This handles some tasks we always want to make
// sure we handle before dropping into the common platform specific halt
// routine.
void platform_halt(platform_halt_action suggested_action, zircon_crash_reason_t reason) {
  // Disable the automatic uptime updating.  We are going to attempt to
  // deliberately halt the system, and we don't want the crashlog to indicate a
  // spontaneous reboot.
  platform_enable_crashlog_uptime_updates(false);

  // We are haling on purpose.  Disable the watchdog (if we have one, and if we
  // can) if we plan to halt instead of instigate a reboot.  If we are going to
  // try to actually reboot, pet the dog one last time to give ourselves the
  // maximum amount to arrange our graceful reboot.
  bool halt_on_panic = gCmdline.GetBool("kernel.halt-on-panic", false);
  if (ENABLE_PANIC_SHELL || halt_on_panic) {
    hw_watchdog_set_enabled(false);
  } else {
    hw_watchdog_pet();
  }

  // Was this an OOM or Panic condition?  If so, render the payload of our
  // crashlog before stowing our reason.  Then, whether we have a payload or
  // not, stow our final crashlog.
  size_t rendered_crashlog_len = 0;
  if ((reason == ZirconCrashReason::Oom) || (reason == ZirconCrashReason::Panic)) {
    memset(crashlog_render_buffer, 0, sizeof(crashlog_render_buffer));
    rendered_crashlog_len =
        crashlog_to_string(crashlog_render_buffer, sizeof(crashlog_render_buffer), reason);
  }
  platform_stow_crashlog(reason, crashlog_render_buffer, rendered_crashlog_len);

  // Finally, fall into the platform specific halt handler.
  platform_specific_halt(suggested_action, reason, halt_on_panic);
}

/*
 * default implementations of these routines, if the platform code
 * chooses not to implement.
 */
__WEAK void platform_specific_halt(platform_halt_action suggested_action,
                                   zircon_crash_reason_t reason, bool halt_on_panic) {
  thread_print_current_backtrace();

  if constexpr (ENABLE_PANIC_SHELL) {
    if (reason == ZirconCrashReason::Panic) {
      dprintf(ALWAYS, "CRASH: starting debug shell... (reason = %d)\n", static_cast<int>(reason));
      arch_disable_ints();
      panic_shell_start();
    }
  }

  dprintf(ALWAYS, "HALT: spinning forever... (reason = %d)\n", static_cast<int>(reason));
  arch_disable_ints();
  for (;;) {
  }
}

__WEAK void platform_halt_cpu() {}
