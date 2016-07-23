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

#include <ddk/driver.h>
#include <ddk/binding.h>
#include <hw/usb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <runtime/mutex.h>
#include <system/listnode.h>

#include "generic-hub.h"
#include "usb-private.h"
#include "usb-device.h"

#include <assert.h>

#define INTR_REQ_COUNT 4

#define DR_PORT (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER)

/* status (and status change) bits */
#define PORT_CONNECTION 0x1
#define PORT_ENABLE 0x2
#define PORT_RESET 0x10
/* feature selectors (for setting / clearing features) */
#define SEL_PORT_RESET 0x4
#define SEL_PORT_POWER 0x8
#define SEL_C_PORT_CONNECTION 0x10
/* request type (USB 3.0 hubs only) */
#define SET_HUB_DEPTH 12

typedef struct usb_hub {
    // the device we are publishing
    mx_device_t hub_device;

    // Underlying USB device
    mx_device_t* device;
    usb_device_protocol_t* device_protocol;

    list_node_t free_intr_reqs;
    mxr_mutex_t mutex;

    usb_speed speed;
    int num_ports;
    generic_hub_t generic_hub;
} usb_hub_t;
#define get_hub(dev) containerof(dev, usb_hub_t, hub_device)

static void queue_interrupt_requests_locked(usb_hub_t* hub) {
    list_node_t* node;
    while ((node = list_remove_head(&hub->free_intr_reqs)) != NULL) {
        usb_request_t* req = containerof(node, usb_request_t, node);
        req->transfer_length = req->buffer_length;
        mx_status_t status = hub->device_protocol->queue_request(hub->device, req);
        if (status != NO_ERROR) {
            printf("interrupt queue failed %d\n", status);
            list_add_head(&hub->free_intr_reqs, &req->node);
            break;
        }
    }
}

static int
usb_hub_port_status_changed(mx_device_t* device, const int port) {
    usb_hub_t* hub = get_hub(device);

    unsigned short buf[2];
    int ret = usb_get_status(hub->device, port, DR_PORT, sizeof(buf), buf);
    if (ret >= 0) {
        ret = buf[1] & PORT_CONNECTION;
        if (ret)
            usb_clear_feature(hub->device, port, SEL_C_PORT_CONNECTION,
                              DR_PORT);
    }
    return ret;
}

static int
usb_hub_port_connected(mx_device_t* device, const int port) {
    usb_hub_t* hub = get_hub(device);

    unsigned short buf[2];
    int ret = usb_get_status(hub->device, port, DR_PORT, sizeof(buf), buf);
    if (ret >= 0)
        ret = buf[0] & PORT_CONNECTION;
    return ret;
}

static int
usb_hub_port_in_reset(mx_device_t* device, const int port) {
    usb_hub_t* hub = get_hub(device);

    unsigned short buf[2];
    int ret = usb_get_status(hub->device, port, DR_PORT, sizeof(buf), buf);
    if (ret >= 0)
        ret = buf[0] & PORT_RESET;
    return ret;
}

static int
usb_hub_port_enabled(mx_device_t* device, const int port) {
    usb_hub_t* hub = get_hub(device);

    unsigned short buf[2];
    int ret = usb_get_status(hub->device, port, DR_PORT, sizeof(buf), buf);
    if (ret >= 0)
        ret = buf[0] & PORT_ENABLE;
    return ret;
}

static usb_speed
usb_hub_port_speed(mx_device_t* device, const int port) {
    usb_hub_t* hub = get_hub(device);

    unsigned short buf[2];
    int ret = usb_get_status(hub->device, port, DR_PORT, sizeof(buf), buf);
    if (ret >= 0 && (buf[0] & PORT_ENABLE)) {
        /* SuperSpeed hubs can only have SuperSpeed devices. */
        if (hub->speed == SUPER_SPEED)
            return SUPER_SPEED;

        /*[bit] 10  9  (USB 2.0 port status word)
		 *      0   0  full speed
		 *      0   1  low speed
		 *      1   0  high speed
		 *      1   1  invalid
		 */
        ret = (buf[0] >> 9) & 0x3;
        if (ret != 0x3)
            return ret;
    }
    return -1;
}

static int
usb_hub_enable_port(mx_device_t* device, const int port) {
    usb_hub_t* hub = get_hub(device);
    return usb_set_feature(hub->device, port, SEL_PORT_POWER, DR_PORT);
}

static int
usb_hub_start_port_reset(mx_device_t* device, const int port) {
    usb_hub_t* hub = get_hub(device);
    return usb_set_feature(hub->device, port, SEL_PORT_RESET, DR_PORT);
}

static int
usb_hub_resetport(mx_device_t* device, const int port) {
    if (usb_hub_start_port_reset(device, port) < 0)
        return -1;

    /* wait for 10ms (usb20 spec 11.5.1.5: reset should take 10 to 20ms) */
    usleep(1000 * 10);

    // FIXME which device here?
    /* now wait 12ms for the hub to finish the reset */
    const int ret = generic_hub_wait_for_port(
        /* time out after 120 * 100us = 12ms */
        device, port, 0, usb_hub_port_in_reset, 120, 100);
    if (ret < 0)
        return -1;
    else if (!ret)
        usb_debug("generic_hub: Reset timed out at port %d\n", port);

    return 0; /* ignore timeouts, try to always go on */
}

static int usb_hub_get_num_ports(mx_device_t* device) {
    usb_hub_t* hub = get_hub(device);
    return hub->num_ports;
}

// FIXME this is broken now
static void usb_hub_set_hub_depth(mx_device_t* device) {
    printf("usb_hub_set_hub_depth\n");
    int hub_depth = 0;
    mx_device_t* parent = device->parent;
    while (parent) {
        void* proto;
        // stop when we find an HCI device, since we don't count root hub in this calculation
        mx_status_t status = device_get_protocol(device, MX_PROTOCOL_USB_HCI, &proto);
        if (status == NO_ERROR)
            break;
        status = device_get_protocol(device, MX_PROTOCOL_USB_HUB, &proto);
        if (status == NO_ERROR)
            hub_depth++;
        parent = parent->parent;
    }
    printf("set hub depth %d\n", hub_depth);

    usb_device_protocol_t* device_proto;
    device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&device_proto);
    int ret = device_proto->control(device, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE, SET_HUB_DEPTH, hub_depth, 0, NULL, 0);
    if (ret < 0)
        usb_debug("Failed SET_HUB_DEPTH(%d): %d\n", hub_depth, ret);
}

static usb_hub_protocol_t usb_hub_protocol = {
    .port_status_changed = usb_hub_port_status_changed,
    .port_connected = usb_hub_port_connected,
    .port_enabled = usb_hub_port_enabled,
    .port_speed = usb_hub_port_speed,
    .enable_port = usb_hub_enable_port,
    .disable_port = NULL,
    .reset_port = usb_hub_resetport,
    .get_num_ports = usb_hub_get_num_ports,
};

static void usb_hub_interrupt_complete(usb_request_t* request) {
    if (request->status < 0) {
        printf("usb_hub_interrupt_complete got %d\n", request->status);
        return;
    }
    usb_hub_t* hub = (usb_hub_t*)request->client_data;

    uint8_t* bitmap = request->buffer;
    uint8_t* bitmap_end = bitmap + request->transfer_length;

    // bit zero is hub status
    if (bitmap[0] & 1) {
        // what to do here?
        printf("usb_hub_interrupt_complete hub status changed\n");
    }

    int port = 1;
    int bit = 1;
    while (bitmap < bitmap_end) {
        if (*bitmap & (1 << bit)) {
            unsigned short buf[2];
            int ret = usb_get_status(hub->device, port, DR_PORT, sizeof(buf), buf);

            if (ret >= 0) {
                if ((buf[0] & PORT_CONNECTION) && (buf[1] & PORT_CONNECTION)) {
                    generic_hub_attach_dev(&hub->generic_hub, port);
                    usb_clear_feature(hub->device, port, SEL_C_PORT_CONNECTION, DR_PORT);
                } else if (!(buf[0] & PORT_CONNECTION) && (buf[1] & PORT_CONNECTION)) {
                    generic_hub_detach_dev(&hub->generic_hub, port);
                    usb_clear_feature(hub->device, port, SEL_C_PORT_CONNECTION, DR_PORT);
                }
            }
        }
        port++;
        if (++bit == 8) {
            bitmap++;
            bit = 0;
        }
    }

    mxr_mutex_lock(&hub->mutex);
    list_add_head(&hub->free_intr_reqs, &request->node);
    queue_interrupt_requests_locked(hub);
    mxr_mutex_unlock(&hub->mutex);
}

static mx_status_t usb_hub_release(mx_device_t* device) {
    usb_hub_t* hub = get_hub(device);
    generic_hub_destroy(&hub->generic_hub);
    free(hub);

    return NO_ERROR;
}

static mx_protocol_device_t usb_hub_device_proto = {
    .release = usb_hub_release,
};

static mx_device_t* usb_get_bus(mx_device_t* device) {
    while (device) {
        usb_bus_protocol_t* bus_protocol;
        if (device_get_protocol(device, MX_PROTOCOL_USB_BUS, (void**)&bus_protocol) == NO_ERROR) {
            return device;
        }
        device = device->parent;
    }
    return NULL;
}

static mx_status_t usb_hub_bind(mx_driver_t* driver, mx_device_t* device) {
    usb_device_protocol_t* device_protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&device_protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    usb_device_config_t* device_config;
    mx_status_t status = device_protocol->get_config(device, &device_config);
    if (status < 0)
        return status;

    // find our interrupt endpoint
    usb_configuration_t* config = &device_config->configurations[0];
    usb_interface_t* intf = &config->interfaces[0];
    if (intf->num_endpoints != 1) {
        printf("usb_ethernet_bind wrong number of endpoints: %d\n", intf->num_endpoints);
        return ERR_NOT_SUPPORTED;
    }
    usb_endpoint_t* endp = &intf->endpoints[0];
    if (endp->type != USB_ENDPOINT_INTERRUPT) {
        return ERR_NOT_SUPPORTED;
    }

    usb_hub_t* hub = calloc(1, sizeof(usb_hub_t));
    if (!hub) {
        printf("Not enough memory for usb_hub_t.\n");
        return ERR_NO_MEMORY;
    }

    status = device_init(&hub->hub_device, driver, "usb_hub", &usb_hub_device_proto);
    if (status != NO_ERROR) {
        free(hub);
        return status;
    }

    hub->hub_device.protocol_id = MX_PROTOCOL_USB_HUB;
    hub->hub_device.protocol_ops = &usb_hub_protocol;

    hub->device = device;
    hub->device_protocol = device_protocol;
    hub->speed = device_protocol->get_speed(device);
    int address = device_protocol->get_address(device);

    int type = hub->speed == SUPER_SPEED ? 0x2a : 0x29; /* similar enough */
    usb_hub_descriptor_t desc;                          /* won't fit the whole thing, we don't care */
    if (usb_get_descriptor(hub->device, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE,
                           type, 0, &desc, sizeof(desc)) < 0) {
        usb_debug("get_descriptor(HUB) failed\n");
        return -1;
    }
    hub->num_ports = desc.bNbrPorts;

    list_initialize(&hub->free_intr_reqs);
    for (int i = 0; i < INTR_REQ_COUNT; i++) {
        usb_request_t* req = device_protocol->alloc_request(device, endp, endp->maxpacketsize);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = usb_hub_interrupt_complete;
        req->client_data = hub;
        list_add_head(&hub->free_intr_reqs, &req->node);
    }

    if (hub->speed == SUPER_SPEED)
        usb_hub_set_hub_depth(hub->device);

    generic_hub_init(&hub->generic_hub, &hub->hub_device, &usb_hub_protocol, usb_get_bus(device),
                     address);
    device_set_bindable(&hub->hub_device, false);
    device_add(&hub->hub_device, device);

    mxr_mutex_lock(&hub->mutex);
    queue_interrupt_requests_locked(hub);
    mxr_mutex_unlock(&hub->mutex);

    return NO_ERROR;
}

static mx_status_t usb_hub_unbind(mx_driver_t* drv, mx_device_t* dev) {
    //TODO: should avoid using dev->childern
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&dev->children, child, temp, mx_device_t, node) {
        device_remove(child);
    }
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_DEVICE),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HUB),
};

mx_driver_t _driver_usb_hub BUILTIN_DRIVER = {
    .name = "usb-hub",
    .ops = {
        .bind = usb_hub_bind,
        .unbind = usb_hub_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
