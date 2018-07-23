// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include "debug.h"

// #define QEMU_PRINT 1
// #define HIKEY960_PRINT 1
// #define VIM2_PRINT 1

#if QEMU_PRINT
static volatile uint32_t* uart_fifo_dr = (uint32_t *)0x09000000;
static volatile uint32_t* uart_fifo_fr = (uint32_t *)0x09000018;
#elif HIKEY960_PRINT
static volatile uint32_t* uart_fifo_dr = (uint32_t *)0xfff32000;
static volatile uint32_t* uart_fifo_fr = (uint32_t *)0xfff32018;
#endif

#if QEMU_PRINT || HIKEY960_PRINT
static void uart_pputc(char c)
{
    /* spin while fifo is full */
    while (*uart_fifo_fr & (1<<5))
        ;
    *uart_fifo_dr = c;
}
#endif

#if VIM2_PRINT
static volatile uint32_t* uart_wfifo = (uint32_t *)0xc81004c0;
static volatile uint32_t* uart_status = (uint32_t *)0xc81004cc;

static void uart_pputc(char c)
{
    /* spin while fifo is full */
    while (*uart_status & (1 << 21))
        ;
    *uart_wfifo = c;
}
#endif

#if QEMU_PRINT || HIKEY960_PRINT || VIM2_PRINT
void uart_puts(const char* str) {
    char ch;
    while ((ch = *str++)) {
        uart_pputc(ch);
    }
}

void uart_print_hex(uint64_t value) {
    const char digits[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (int i = 60; i >= 0; i -= 4) {
        uart_pputc(digits[(value >> i) & 0xf]);
    }
}
#else
void uart_puts(const char* str) {
}

void uart_print_hex(uint64_t value) {
}
#endif
