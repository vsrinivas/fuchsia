// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_UART_IMX_INCLUDE_DEV_UART_IMX_INIT_H_
#define ZIRCON_KERNEL_DEV_UART_IMX_INCLUDE_DEV_UART_IMX_INIT_H_

#include <zircon/boot/driver-config.h>

// Initialization routines at the PLATFORM_EARLY and PLATFORM levels.
void ImxUartInitEarly(const zbi_dcfg_simple_t& config);
void ImxUartInitLate();

#endif  // ZIRCON_KERNEL_DEV_UART_IMX_INCLUDE_DEV_UART_IMX_INIT_H_
