// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>
#include <platform/halt_helper.h>

void platform_graceful_halt_helper(platform_halt_action action) {
    thread_migrate_to_cpu(BOOT_CPU_ID);
    platform_halt_secondary_cpus();

    // Delay shutdown of debuglog to ensure log messages emitted by above calls will be written.
    dlog_shutdown();

    platform_halt(action, HALT_REASON_SW_RESET);
    panic("ERROR: failed to halt the platform\n");
}
