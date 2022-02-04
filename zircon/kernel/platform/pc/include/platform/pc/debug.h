// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_DEBUG_H_
#define ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_DEBUG_H_

#include <lib/uart/all.h>

// Initializes a 16550-compatible UART at the PLATFORM_EARLY and PLATFORM
// levels:
void X86UartInitEarly(const uart::all::Driver& serial);
void X86UartInitLate();

void pc_suspend_debug();
void pc_resume_debug();

#endif  // ZIRCON_KERNEL_PLATFORM_PC_INCLUDE_PLATFORM_PC_DEBUG_H_
