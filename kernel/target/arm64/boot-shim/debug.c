// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include "debug.h"

// #define QEMU_PRINT

#ifdef QEMU_PRINT
static volatile uint32_t* uart_fifo_dr = (uint32_t *)0x09000000;
static volatile uint32_t* uart_fifo_fr = (uint32_t *)0x09000018;

static void uart_pputc(char c)
{
    /* spin while fifo is full */
    while (*uart_fifo_fr & (1<<5))
        ;
    *uart_fifo_dr = c;
}

void uart_puts(const char* str) {
    char ch;
    while ((ch = *str++)) {
        uart_pputc(ch);
    }
}
#else
void uart_puts(const char* str) {
}
#endif
