// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "debug.h"

#define UART_BASE (0x10A00000)
static volatile uint32_t* const uart_fstat = (uint32_t*)(UART_BASE + 0x18);
static volatile uint32_t* const uart_tx = (uint32_t*)(UART_BASE + 0x20);

void uart_pputc(char c) {
  // spin while fifo is full
  while (*uart_fstat & (1 << 24))
    ;
  *uart_tx = c;
}
