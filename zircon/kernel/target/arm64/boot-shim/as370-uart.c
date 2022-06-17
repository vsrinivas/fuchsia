// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>

#include "debug.h"

#define UART_THR (0x0)   // TX Buffer Register (write-only)
#define UART_LSR (0x14)  // Line Status Register
#define UART_LSR_THRE (1 << 5)

#define UARTREG(reg) (*(volatile uint32_t*)(0xf7e80c00 + (reg)))

void uart_pputc(char c) {
  while (!(UARTREG(UART_LSR) & UART_LSR_THRE))
    ;
  UARTREG(UART_THR) = c;
}
