// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <kernel/timer.h>
#include <stdio.h>
#include "target_p.h"

/* declared in platform/pc/debug.c */
extern enum handler_return platform_drain_debug_uart_rx(void);

/* since the com1 IRQs do not work on pixel hardware, run a timer to poll for incoming
 * characters.
 */
static timer_t uart_rx_poll_timer;

static enum handler_return uart_rx_poll(struct timer *t, lk_time_t now, void *arg)
{
    return platform_drain_debug_uart_rx();
}

void pc_debug_init(void)
{
    /* The Pixel2 does not have the serial RX IRQ wired up for the debug UART. */

    printf("Enabling Debug UART RX Hack\n");
    timer_initialize(&uart_rx_poll_timer);
    timer_set_periodic(&uart_rx_poll_timer, 10, uart_rx_poll, NULL);
}

