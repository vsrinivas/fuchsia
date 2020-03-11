// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_HELPER_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_HELPER_H_

#include <platform.h>
#include <zircon/boot/crash-reason.h>

// Gracefully halt and perform |action|.
void platform_graceful_halt_helper(platform_halt_action action, zircon_crash_reason_t);

// Graefully halt secondary (non-boot) CPUs.
//
// While the mechanism used is platform dependent, this function attempts to shut them down
// gracefully so that secondary CPUs aren't holding any kernel locks.
//
// This function must be called from the primary (boot) CPU.
zx_status_t platform_halt_secondary_cpus();

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_HELPER_H_
