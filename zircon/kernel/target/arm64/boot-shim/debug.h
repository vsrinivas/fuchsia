// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_TARGET_ARM64_BOOT_SHIM_DEBUG_H_
#define ZIRCON_KERNEL_TARGET_ARM64_BOOT_SHIM_DEBUG_H_

#include <stdint.h>

// Uncomment to enable debug UART.
// #define DEBUG_UART 1

// Board specific.
void uart_pputc(char c);

// Common code.
void uart_puts(const char* str);
void uart_print_hex(uint64_t value);

#endif  // ZIRCON_KERNEL_TARGET_ARM64_BOOT_SHIM_DEBUG_H_
