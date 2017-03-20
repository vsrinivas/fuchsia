// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <arch/arch_ops.h>
#include <pdev/uart.h>

static int default_putc(char c) {
    return -1;
}

static int default_getc(bool wait) {
    return -1;
}

static int default_pputc(char c) {
    return -1;
}

static int default_pgetc(void) {
    return -1;
}

static const struct pdev_uart_ops default_ops = {
    .putc = default_putc,
    .getc = default_getc,
    .pputc = default_pputc,
    .pgetc = default_pgetc,
};

static const struct pdev_uart_ops* uart_ops = &default_ops;

void uart_init(void) {
}

void uart_init_early(void) {
}

int uart_putc(char c) {
    return uart_ops->putc(c);
}

int uart_getc(bool wait)
{
    return uart_ops->getc(wait);
}

int uart_pputc(char c) {
    return uart_ops->pputc(c);
}

int uart_pgetc(void) {
    return uart_ops->pgetc();
}

void pdev_register_uart(const struct pdev_uart_ops* ops) {
    uart_ops = ops;
    smp_mb();
}
