// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_DEBUG_H_
#define ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_DEBUG_H_

#include <stdint.h>

// Uncomment to enable debug UART.
// #define DEBUG_UART 1

// Board specific.
void uart_pputc(char c);

// Common code.
#if DEBUG_UART
void uart_puts(const char* str);
void uart_putc(char c);
void uart_print_hex(uint64_t value);
#else
static inline void uart_puts(const char* str) {}
static inline void uart_putc(char ch) {}

static inline void uart_print_hex(uint64_t value) {}
#endif

#endif  // ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_DEBUG_H_
