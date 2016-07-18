// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <acpica/acpi.h>
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

static ACPI_STATUS acpi_get_cros_ec_device(
        ACPI_HANDLE object,
        UINT32 nesting_level,
        void *context,
        void **ret)
{
    bool *has_cros_ec = context;
    *has_cros_ec = true;
    return AE_OK;
}

bool has_cros_embedded_controller(void)
{
    bool has_cros_ec = false;
    __UNUSED ACPI_STATUS status = AcpiGetDevices(
            (char*)"GOOG0003", // CrOS EC PD
            acpi_get_cros_ec_device,
            &has_cros_ec,
            NULL);
    DEBUG_ASSERT(status == NO_ERROR);
    return has_cros_ec;
}

void pc_debug_init(void)
{
    /* The Pixel2 does not have the serial RX IRQ wired up for the debug UART.
     * Try to detect if we're a Pixel2 by checking for a CrOS embedded
     * controller so we can poll instead of waiting for RX interrupts. */
    if (has_cros_embedded_controller()) {
        printf("Enabling Debug UART RX Hack\n");
        /* poll for input periodically in case rx interrupts are broken */
        timer_initialize(&uart_rx_poll_timer);
        timer_set_periodic(&uart_rx_poll_timer, 10, uart_rx_poll, NULL);
    }
}

