// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "debug.h"

#include <stdint.h>

#if DEBUG_UART
void uart_puts(const char* str) {
  char ch;
  while ((ch = *str++)) {
    uart_putc(ch);
  }
}

void uart_putc(char ch) {
  if (ch == '\n')
    uart_pputc('\r');
  uart_pputc(ch);
}

void uart_print_hex(uint64_t value) {
  const char digits[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  for (int i = 60; i >= 0; i -= 4) {
    uart_pputc(digits[(value >> i) & 0xf]);
  }
}
#endif
