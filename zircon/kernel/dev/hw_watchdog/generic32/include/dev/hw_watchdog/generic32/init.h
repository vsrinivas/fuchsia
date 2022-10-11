// Copyright 2021 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_HW_WATCHDOG_GENERIC32_INCLUDE_DEV_HW_WATCHDOG_GENERIC32_INIT_H_
#define ZIRCON_KERNEL_DEV_HW_WATCHDOG_GENERIC32_INCLUDE_DEV_HW_WATCHDOG_GENERIC32_INIT_H_

#include <zircon/boot/driver-config.h>

// Early (single-threaded) and late (multi-threaded) initialization routines
// for the driver.
void Generic32BitWatchdogEarlyInit(const zbi_dcfg_generic32_watchdog_t& config);
void Generic32BitWatchdogLateInit();

#endif  // ZIRCON_KERNEL_DEV_HW_WATCHDOG_GENERIC32_INCLUDE_DEV_HW_WATCHDOG_GENERIC32_INIT_H_
