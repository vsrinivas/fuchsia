// Copyright 2021 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_HW_RNG_AMLOGIC_RNG_INCLUDE_DEV_HW_RNG_AMLOGIC_RNG_INIT_H_
#define ZIRCON_KERNEL_DEV_HW_RNG_AMLOGIC_RNG_INCLUDE_DEV_HW_RNG_AMLOGIC_RNG_INIT_H_

#include <zircon/boot/driver-config.h>

// Initializes the driver.
void AmlogicRngInit(const dcfg_amlogic_rng_driver_t& config);

#endif  // ZIRCON_KERNEL_DEV_HW_RNG_AMLOGIC_RNG_INCLUDE_DEV_HW_RNG_AMLOGIC_RNG_INIT_H_
