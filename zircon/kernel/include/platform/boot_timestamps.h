// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_BOOT_TIMESTAMPS_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_BOOT_TIMESTAMPS_H_

#include <lib/arch/ticks.h>
#include <zircon/compiler.h>

// Definitions of some timestamps which will be recorded after physboot runs, as
// the kernel boots, which eventually get exported as kcounters to allow metrics
// on how long it takes to get through various portions of early boot.

__BEGIN_CDECLS

// Samples taken at the first instruction in the kernel.
extern arch::EarlyTicks kernel_entry_ticks;
// ... and at the entry to normal virtual-space kernel code.
extern arch::EarlyTicks kernel_virtual_entry_ticks;

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_BOOT_TIMESTAMPS_H_
