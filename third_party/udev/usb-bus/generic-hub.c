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

//#define USB_DEBUG

#include <ddk/device.h>
#include <stdlib.h>
#include <unistd.h>

#include "generic-hub.h"
#include "usb-private.h"

void generic_hub_destroy(generic_hub_t* hub) {
    /* First, detach all devices behind this hub */
    int port;
    for (port = 1; port <= hub->num_ports; ++port) {
        mx_device_t* device = hub->ports[port];
        if (hub->ports[port]) {
            hub->bus_protocol->detach_device(hub->busdev, device);
            hub->ports[port] = NULL;
        }
    }

    /* Disable all ports */
    if (hub->hub_protocol->disable_port) {
        for (port = 1; port <= hub->num_ports; ++port)
            hub->hub_protocol->disable_port(hub->hubdev, port);
    }

    free(hub->ports);
    free(hub);
}

static int
generic_hub_debounce(generic_hub_t* hub, const int port) {
    const int step_ms = 1;       /* linux uses 25ms, we're busy anyway */
    const int at_least_ms = 100; /* 100ms as in usb20 spec 9.1.2 */
    const int timeout_ms = 1500; /* linux uses this value */

    int total_ms = 0;
    int stable_ms = 0;
    while (stable_ms < at_least_ms && total_ms < timeout_ms) {
        usleep(1000 * step_ms);

        const int changed = hub->hub_protocol->port_status_changed(hub->hubdev, port);
        const int connected = hub->hub_protocol->port_connected(hub->hubdev, port);
        if (changed < 0 || connected < 0)
            return -1;

        if (!changed && connected) {
            stable_ms += step_ms;
        } else {
            usb_debug("generic_hub: Unstable connection at %d\n",
                      port);
            stable_ms = 0;
        }
        total_ms += step_ms;
    }
    if (total_ms >= timeout_ms)
        usb_debug("generic_hub: Debouncing timed out at %d\n", port);
    return 0; /* ignore timeouts, try to always go on */
}

int generic_hub_wait_for_port(mx_device_t* device, const int port,
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

int generic_hub_detach_dev(generic_hub_t* const hub, const int port) {
    mx_device_t* device = hub->ports[port];
    if (!device)
        return -1;

    hub->bus_protocol->detach_device(hub->busdev, device);
    device_remove(device);
    hub->ports[port] = NULL;

    return 0;
}

int generic_hub_attach_dev(generic_hub_t* const hub, const int port) {
    if (hub->ports[port])
        return 0;

    if (generic_hub_debounce(hub, port) < 0)
        return -1;

    if (hub->hub_protocol->reset_port) {
        if (hub->hub_protocol->reset_port(hub->hubdev, port) < 0)
            return -1;
        /* after reset the port will be enabled automatically */
        const int ret = generic_hub_wait_for_port(
            /* time out after 1,000 * 10us = 10ms */
            hub->hubdev, port, 1, hub->hub_protocol->port_enabled, 1000, 10);
        if (ret < 0)
            return -1;
        else if (!ret)
            usb_debug(
                "generic_hub: Port %d still "
                "disabled after 10ms\n",
                port);
    }

    const usb_speed_t speed = hub->hub_protocol->port_speed(hub->hubdev, port);
    if (hub->hub_protocol->reset_port)
        usleep(1000 * 10); /* Reset recovery time
			       (usb20 spec 7.1.7.5) */
    hub->ports[port] = hub->bus_protocol->attach_device(hub->busdev, hub->hubdev, hub->hub_address,
                                                        port, speed);

    return 0;
}

int generic_hub_scanport(generic_hub_t* const hub, const int port) {
    if (hub->ports[port]) {
        usb_debug("generic_hub: Detachment at port %d\n", port);

        const int ret = generic_hub_detach_dev(hub, port);
        if (ret < 0)
            return ret;
    }

    if (hub->hub_protocol->port_connected(hub->hubdev, port)) {
        usb_debug("generic_hub: Attachment at port %d\n", port);

        return generic_hub_attach_dev(hub, port);
    }

    return 0;
}

int generic_hub_init(generic_hub_t* hub, mx_device_t* hubdev, usb_hub_protocol_t* hub_protocol,
                     mx_device_t* busdev, int hub_address) {
    usb_bus_protocol_t* bus_protocol;
    device_get_protocol(busdev, MX_PROTOCOL_USB_BUS, (void**)&bus_protocol);

    hub->hub_address = hub_address;
    hub->num_ports = hub_protocol->get_num_ports(hubdev);
    hub->hubdev = hubdev;
    hub->busdev = busdev;
    hub->hub_protocol = hub_protocol;
    hub->bus_protocol = bus_protocol;

    hub->ports = calloc(hub->num_ports + 1, sizeof(*hub->ports));
    if (!hub->ports) {
        usb_debug("generic_hub: ERROR: Out of memory\n");
        return -1;
    }

    /* Enable all ports */
    if (hub_protocol->enable_port) {
        for (int port = 1; port <= hub->num_ports; ++port)
            hub_protocol->enable_port(hubdev, port);
        /* wait once for all ports */
        usleep(1000 * 20);
    }

    return 0;
}
