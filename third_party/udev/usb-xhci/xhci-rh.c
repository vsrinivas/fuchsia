/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2013 secunet Security Networks AG
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define USB_DEBUG

#include <ddk/protocol/usb-hub.h>
#include <ddk/protocol/usb-bus.h>
#include <unistd.h>

#include "xhci-private.h"

static int
xhci_rh_port_status_changed_internal(xhci_t* xhci, const int port) {
    volatile uint32_t* const portsc = &xhci->opreg->prs[port - 1].portsc;

    const int changed = !!(*portsc & (PORTSC_CSC | PORTSC_PRC));
    /* always clear all the status change bits */
    *portsc = (*portsc & PORTSC_RW_MASK) | 0x00fe0000;
    return changed;
}

static int
xhci_rh_port_status_changed(mx_device_t* device, const int port) {
    xhci_t* xhci = get_xhci(device);
    return xhci_rh_port_status_changed_internal(xhci, port);
}

void xhci_rh_check_status_changed(xhci_t* xhci) {
    const int changed = !!(xhci->opreg->usbsts & USBSTS_PCD);
    if (changed) {
        printf("root hub status change\n");
        xhci->opreg->usbsts =
            (xhci->opreg->usbsts & USBSTS_PRSRV_MASK) | USBSTS_PCD;
        if (xhci->bus_device) {
            for (int port = 1; port <= xhci->num_rh_ports; ++port) {
                if (xhci_rh_port_status_changed_internal(xhci, port) == 1) {
                    xhci->bus_protocol->root_hub_port_changed(xhci->bus_device, port);
                }
            }
        } else {
            printf("no bus device in xhci_rh_check_status_changed\n");
        }
    }
}

static int
xhci_rh_port_connected(mx_device_t* device, const int port) {
    xhci_t* const xhci = get_xhci(device);
    volatile uint32_t* const portsc = &xhci->opreg->prs[port - 1].portsc;

    return *portsc & PORTSC_CCS;
}

static int
xhci_rh_port_in_reset(mx_device_t* device, const int port) {
    xhci_t* const xhci = get_xhci(device);
    volatile uint32_t* const portsc = &xhci->opreg->prs[port - 1].portsc;

    return !!(*portsc & PORTSC_PR);
}

static int
xhci_rh_port_enabled(mx_device_t* device, const int port) {
    xhci_t* const xhci = get_xhci(device);
    volatile uint32_t* const portsc = &xhci->opreg->prs[port - 1].portsc;

    return !!(*portsc & PORTSC_PED);
}

static usb_speed
xhci_rh_port_speed(mx_device_t* device, const int port) {
    xhci_t* const xhci = get_xhci(device);
    volatile uint32_t* const portsc = &xhci->opreg->prs[port - 1].portsc;

    if (*portsc & PORTSC_PED) {
        return ((*portsc & PORTSC_PORT_SPEED_MASK) >> PORTSC_PORT_SPEED_START) - 1;
    } else {
        return -1;
    }
}

static int
xhci_wait_for_port(mx_device_t* device, const int port,
                   const int wait_for,
                   int (*const port_op)(mx_device_t*, int),
                   int timeout_steps, const int step_us) {
    int state;
    int step_ms;
    if (step_us > 1000) {
        step_ms = step_us / 1000;
    } else {
        step_ms = 1;
        timeout_steps *= (1000 / step_us);
    }
    do {
        state = port_op(device, port);
        if (state < 0)
            return -1;
        else if (!!state == wait_for)
            return timeout_steps;
        usleep(1000 * step_ms);
        --timeout_steps;
    } while (timeout_steps);
    return 0;
}

static int
xhci_rh_reset_port(mx_device_t* device, const int port) {
    xhci_t* const xhci = get_xhci(device);
    volatile uint32_t* const portsc = &xhci->opreg->prs[port - 1].portsc;

    /* Trigger port reset. */
    *portsc = (*portsc & PORTSC_RW_MASK) | PORTSC_PR;

    /* Wait for port_in_reset == 0, up to 150 * 1000us = 150ms */
    if (xhci_wait_for_port(device, port, 0, xhci_rh_port_in_reset,
                           150, 1000) == 0)
        xhci_debug("xhci_rh: Reset timed out at port %d\n", port);
    else
        /* Clear reset status bits, since port is out of reset. */
        *portsc = (*portsc & PORTSC_RW_MASK) | PORTSC_PRC | PORTSC_WRC;

    return 0;
}

static int
xhci_rh_enable_port(mx_device_t* device, int port) {
#if 0
    usbdev_t* dev = get_usb_device(device);
	if (IS_ENABLED(CONFIG_LP_USB_XHCI_MTK_QUIRK)) {
		xhci_t *const xhci = XHCI_INST(dev->controller);
		volatile uint32_t *const portsc =
			&xhci->opreg->prs[port - 1].portsc;

		/*
		 * Before sending commands to a port, the Port Power in
		 * PORTSC register should be enabled on MTK's xHCI.
		 */
		*portsc = (*portsc & PORTSC_RW_MASK) | PORTSC_PP;
	}
#endif
    return 0;
}

static int xhci_rh_get_num_ports(mx_device_t* device) {
    xhci_t* xhci = get_xhci(device);
    return xhci->num_rh_ports;
}

usb_hub_protocol_t xhci_rh_hub_protocol = {
    .port_status_changed = xhci_rh_port_status_changed,
    .port_connected = xhci_rh_port_connected,
    .port_enabled = xhci_rh_port_enabled,
    .port_speed = xhci_rh_port_speed,
    .enable_port = xhci_rh_enable_port,
    .disable_port = NULL,
    .reset_port = xhci_rh_reset_port,
    .get_num_ports = xhci_rh_get_num_ports,
};

mx_status_t
xhci_rh_init(usb_xhci_t* uxhci) {
    xhci_t* xhci = &uxhci->xhci;
    usbdev_t* dev = xhci->roothub;

    /* we can set them here because a root hub _really_ shouldn't
	   appear elsewhere */
    dev->address = 0;
    dev->hub = -1;
    dev->port = -1;

    xhci->num_rh_ports = /* TODO: maybe we need to read extended caps */
        (xhci->capreg->hcsparams1 >> 24) & 0xff;

    return NO_ERROR;
}
