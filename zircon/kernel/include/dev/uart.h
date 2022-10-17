// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_DEV_UART_H_
#define ZIRCON_KERNEL_INCLUDE_DEV_UART_H_

#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

void uart_init(void);
void uart_init_early(void);

bool uart_present(void);
void uart_putc(char c);
int uart_getc(bool wait);

/*
 * block : Blocking vs Non-Blocking
 */
void uart_puts(const char* str, size_t len, bool block);

/* panic-time uart accessors, intended to be run with interrupts disabled */
void uart_pputc(char c);
int uart_pgetc(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_DEV_UART_H_
