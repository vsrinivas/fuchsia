// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_UART_MOTMOT_INCLUDE_DEV_UART_MOTMOT_INIT_H_
#define ZIRCON_KERNEL_DEV_UART_MOTMOT_INCLUDE_DEV_UART_MOTMOT_INIT_H_

#include <zircon/boot/driver-config.h>

// Initialization routines at the PLATFORM_EARLY and PLATFORM levels.
void MotmotUartInitEarly(const dcfg_simple_t& config);
void MotmotUartInitLate();

#endif  // ZIRCON_KERNEL_DEV_UART_MOTMOT_INCLUDE_DEV_UART_MOTMOT_INIT_H_
