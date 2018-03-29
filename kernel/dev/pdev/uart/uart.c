// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <arch/arch_ops.h>
#include <pdev/uart.h>

static int default_getc(bool wait) {
    return -1;
}

static int default_pputc(char c) {
    return -1;
}

static int default_pgetc(void) {
    return -1;
}

static void default_start_panic(void) {

}

static void default_dputs(const char* str, size_t len,
                          bool block, bool map_NL) {

}

static const struct pdev_uart_ops default_ops = {
    .getc = default_getc,
    .pputc = default_pputc,
    .pgetc = default_pgetc,
    .start_panic = default_start_panic,
    .dputs = default_dputs,
};

static const struct pdev_uart_ops* uart_ops = &default_ops;

void uart_init(void) {
}

void uart_init_early(void) {
}

bool uart_present(void) {
    return uart_ops != &default_ops;
}

void uart_putc(char c) {
    uart_ops->dputs(&c, 1, true, true);
}

int uart_getc(bool wait)
{
    return uart_ops->getc(wait);
}

/*
 * block : Blocking vs Non-Blocking
 * map_NL : If true, map a '\n' to '\r'+'\n'
 */
void uart_puts(const char* str, size_t len, bool block, bool map_NL)
{
    uart_ops->dputs(str, len, block, map_NL);
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
