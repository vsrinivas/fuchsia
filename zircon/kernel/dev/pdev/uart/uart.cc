// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>
#include <zircon/errors.h>

#include <pdev/uart.h>

static int default_getc(bool wait) { return ZX_ERR_NOT_SUPPORTED; }

static void default_pputc(char c) {}

static int default_pgetc() { return ZX_ERR_NOT_SUPPORTED; }

static void default_start_panic() {}

static void default_dputs(const char* str, size_t len, bool block) {}

static const struct pdev_uart_ops default_ops = {
    .getc = default_getc,
    .pputc = default_pputc,
    .pgetc = default_pgetc,
    .start_panic = default_start_panic,
    .dputs = default_dputs,
};

static const struct pdev_uart_ops* uart_ops = &default_ops;

void uart_init() {}

void uart_init_early() {}

bool uart_present() { return uart_ops != &default_ops; }

void uart_putc(char c) { uart_ops->dputs(&c, 1, true); }

int uart_getc(bool wait) { return uart_ops->getc(wait); }

/*
 * block : Blocking vs Non-Blocking
 */
void uart_puts(const char* str, size_t len, bool block) { uart_ops->dputs(str, len, block); }

void uart_pputc(char c) { uart_ops->pputc(c); }

int uart_pgetc() { return uart_ops->pgetc(); }

void pdev_register_uart(const struct pdev_uart_ops* ops) {
  uart_ops = ops;
  arch::ThreadMemoryBarrier();
}
