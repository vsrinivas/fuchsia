// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>
#include <zircon/boot/crash-reason.h>

#include <kernel/mp.h>
#include <platform/halt_helper.h>

void platform_graceful_halt_helper(platform_halt_action action, zircon_crash_reason_t reason,
                                   zx_time_t panic_deadline) {
  printf("platform_graceful_halt_helper: action=%d reason=%u panic_deadline=%ld current_time=%ld\n",
         action, static_cast<uint32_t>(reason), panic_deadline, current_time());

  // Migrate to the boot CPU before shutting down the secondary CPUs.  Note that
  // this action also hard-pins our thread to the boot CPU, so we don't need to
  // worry about migration after this.
  Thread::Current::MigrateToCpu(BOOT_CPU_ID);
  printf("platform_graceful_halt_helper: Migrated thread to boot CPU.\n");

  zx_status_t status = platform_halt_secondary_cpus(panic_deadline);
  ASSERT_MSG(status == ZX_OK, "platform_halt_secondary_cpus failed: %d\n", status);
  printf("platform_graceful_halt_helper: Halted secondary CPUs.\n");

  // Delay shutdown of debuglog to ensure log messages emitted by above calls will be written.
  printf("platform_graceful_halt_helper: Shutting down dlog.\n");
  status = dlog_shutdown(panic_deadline);
  ASSERT_MSG(status == ZX_OK, "dlog_shutdown failed: %d\n", status);

  printf("platform_graceful_halt_helper: Calling platform_halt.\n");
  platform_halt(action, reason);
  panic("ERROR: failed to halt the platform\n");
}

zx_status_t platform_halt_secondary_cpus(zx_time_t deadline) {
  // Ensure the current thread is pinned to the boot CPU.
  DEBUG_ASSERT(Thread::Current::Get()->hard_affinity_ == cpu_num_to_mask(BOOT_CPU_ID));

  // "Unplug" online secondary CPUs before halting them.
  cpu_mask_t primary = cpu_num_to_mask(BOOT_CPU_ID);
  cpu_mask_t mask = mp_get_online_mask() & ~primary;
  return mp_unplug_cpu_mask(mask, deadline);
}
