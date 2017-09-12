// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <dev/uart.h>

__BEGIN_CDECLS

// UART interface
struct pdev_uart_ops {
    int (*putc)(char c);
    int (*getc)(bool wait);

    /* panic-time uart accessors, intended to be run with interrupts disabled */
    int (*pputc)(char c);
    int (*pgetc)(void);
};

void pdev_register_uart(const struct pdev_uart_ops* ops);

__END_CDECLS
