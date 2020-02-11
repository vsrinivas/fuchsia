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

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_HELPER_H_
