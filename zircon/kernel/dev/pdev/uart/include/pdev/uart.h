// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_PDEV_UART_INCLUDE_PDEV_UART_H_
#define ZIRCON_KERNEL_DEV_PDEV_UART_INCLUDE_PDEV_UART_H_

#include <zircon/compiler.h>

#include <dev/uart.h>

__BEGIN_CDECLS

// UART interface
struct pdev_uart_ops {
  int (*getc)(bool wait);

  /* panic-time uart accessors, intended to be run with interrupts disabled */
  void (*pputc)(char c);
  int (*pgetc)(void);

  void (*start_panic)(void);
  void (*dputs)(const char* str, size_t len, bool block);
};

void pdev_register_uart(const struct pdev_uart_ops* ops);

__END_CDECLS

#endif  // ZIRCON_KERNEL_DEV_PDEV_UART_INCLUDE_PDEV_UART_H_
