// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>
#include <zircon/boot/crash-reason.h>

#include <platform/halt_helper.h>

void platform_graceful_halt_helper(platform_halt_action action, zircon_crash_reason_t reason) {
  thread_migrate_to_cpu(BOOT_CPU_ID);
  platform_halt_secondary_cpus();

  platform_halt(action, reason);
  panic("ERROR: failed to halt the platform\n");
}
