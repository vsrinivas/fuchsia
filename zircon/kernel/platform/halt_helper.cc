// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>
#include <zircon/boot/crash-reason.h>

#include <platform/halt_helper.h>

void platform_graceful_halt_helper(platform_halt_action action, zircon_crash_reason_t reason) {
  // Migrate to the boot CPU before shutting down the secondary CPUs.  Note that
  // this action also hard-pins our thread to the boot CPU, so we don't need to
  // worry about migration after this.
  printf("platform_graceful_halt_helper: Migrating thread to boot CPU.\n");
  Thread::Current::MigrateToCpu(BOOT_CPU_ID);

  printf("platform_graceful_halt_helper: Halting secondary CPUs.\n");
  platform_halt_secondary_cpus();

  // Delay shutdown of debuglog to ensure log messages emitted by above calls will be written.
  printf("platform_graceful_halt_helper: Shutting down dlog.\n");
  dlog_shutdown();

  printf("platform_graceful_halt_helper: Calling platform_halt.\n");
  platform_halt(action, reason);
  panic("ERROR: failed to halt the platform\n");
}
