// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>

#include "debug.h"

static volatile uint32_t* uart_fifo_dr = (uint32_t*)0xfff32000;
static volatile uint32_t* uart_fifo_fr = (uint32_t*)0xfff32018;

void uart_pputc(char c) {
  /* spin while fifo is full */
  while (*uart_fifo_fr & (1 << 5))
    ;
  *uart_fifo_dr = c;
}
