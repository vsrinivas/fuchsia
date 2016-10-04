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
extern void platform_debug_start_uart_timer(void);

void pc_debug_init(void)
{
    /* The Pixel2 does not have the serial RX IRQ wired up for the debug UART. */
    printf("Enabling Debug UART RX Hack\n");
    platform_debug_start_uart_timer();
}

