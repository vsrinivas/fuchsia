// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Gurjant Kalsi
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(gkalsi): Unify the two UART codepaths and use the port parameter to
// select between the real uart and the miniuart.

#include <assert.h>
#include <dev/interrupt.h>
#include <dev/uart.h>
#include <kernel/thread.h>
#include <lib/cbuf.h>
#include <platform/msm8998.h>
#include <platform/debug.h>
#include <reg.h>
#include <stdio.h>
#include <trace.h>

#define RXBUF_SIZE 16

static cbuf_t uart_rx_buf;

int uart_putc(int port, char c) {
    return 1;
}

void uart_init(void) {
}

void uart_init_early(void) {
}

int uart_getc(int port, bool wait) {
    cbuf_t* rxbuf = &uart_rx_buf;

    char c;
    if (cbuf_read_char(rxbuf, &c, wait) == 1)
        return c;

    return -1;
}

void uart_flush_tx(int port) {

}

void uart_flush_rx(int port) {

}

void uart_init_port(int port, uint baud) {
}
