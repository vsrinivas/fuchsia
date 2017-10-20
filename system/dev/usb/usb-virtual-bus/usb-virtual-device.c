// Copyright 2017 The Fuchsia Authors. All riusghts reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-dci.h>
#include <ddk/protocol/usb-function.h>

#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "usb-virtual-bus.h"

typedef struct usb_virtual_device {
    zx_device_t* zxdev;
    usb_virtual_bus_t* bus;
    usb_dci_interface_t dci_intf;
} usb_virtual_device_t;

void usb_virtual_device_control(usb_virtual_device_t* device, usb_request_t* req) {
    usb_setup_t* setup = &req->setup;
    zx_status_t status;
    size_t length = le16toh(setup->wLength);
    size_t actual = 0;

    printf("usb_virtual_device_control type: 0x%02X req: %d value: %d index: %d length: %zu\n",
           setup->bmRequestType, setup->bRequest, le16toh(setup->wValue), le16toh(setup->wIndex),
           length);

    if (device->dci_intf.ops) {
        void* buffer = NULL;

        if (length > 0) {
            usb_request_mmap(req, &buffer);
        }

        status = usb_dci_control(&device->dci_intf, setup, buffer, length, &actual);
    } else {
        status = ZX_ERR_UNAVAILABLE;
    }

    usb_request_complete(req, status, actual);
}

static void device_request_queue(void* ctx, usb_request_t* req) {
    usb_virtual_device_t* device = ctx;
    usb_virtual_bus_device_queue(device->bus, req);
}

static zx_status_t device_set_interface(void* ctx, usb_dci_interface_t* dci_intf) {
    usb_virtual_device_t* device = ctx;
    memcpy(&device->dci_intf, dci_intf, sizeof(device->dci_intf));
    return ZX_OK;
}

static zx_status_t device_config_ep(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                    usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return ZX_OK;
}

static zx_status_t device_disable_ep(void* ctx, uint8_t ep_addr) {
    return ZX_OK;
}

static zx_status_t device_ep_set_stall(void* ctx, uint8_t ep_address) {
    usb_virtual_device_t* device = ctx;
    return usb_virtual_bus_set_stall(device->bus, ep_address, true);
}

static zx_status_t device_ep_clear_stall(void* ctx, uint8_t ep_address) {
    usb_virtual_device_t* device = ctx;
    return usb_virtual_bus_set_stall(device->bus, ep_address, false);
}

usb_dci_protocol_ops_t virt_device_dci_protocol = {
    .request_queue = device_request_queue,
    .set_interface = device_set_interface,
    .config_ep = device_config_ep,
    .disable_ep = device_disable_ep,
    .ep_set_stall = device_ep_set_stall,
    .ep_clear_stall = device_ep_clear_stall,
};

static zx_status_t virt_device_get_initial_mode(void* ctx, usb_mode_t* out_mode) {
    *out_mode = USB_MODE_NONE;
    return ZX_OK;
}

static zx_status_t virt_device_set_mode(void* ctx, usb_mode_t mode) {
    usb_virtual_device_t* device = ctx;
    return usb_virtual_bus_set_mode(device->bus, mode);
}

usb_mode_switch_protocol_ops_t virt_device_ums_protocol = {
    .get_initial_mode = virt_device_get_initial_mode,
    .set_mode = virt_device_set_mode,
};

static zx_status_t virt_device_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    switch (proto_id) {
    case ZX_PROTOCOL_USB_DCI: {
        usb_dci_protocol_t* proto = out;
        proto->ops = &virt_device_dci_protocol;
        proto->ctx = ctx;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        usb_mode_switch_protocol_t* proto = out;
        proto->ops = &virt_device_ums_protocol;
        proto->ctx = ctx;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t virt_device_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    return ZX_OK;
}

static void virt_device_unbind(void* ctx) {
    usb_virtual_device_t* device = ctx;
    device_remove(device->zxdev);
}

static void virt_device_release(void* ctx) {
    usb_virtual_device_t* device = ctx;
    free(device);
}

static zx_protocol_device_t usb_virtual_device_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = virt_device_get_protocol,
    .open = virt_device_open,
    .unbind = virt_device_unbind,
    .release = virt_device_release,
};

zx_status_t usb_virtual_device_add(usb_virtual_bus_t* bus, usb_virtual_device_t** out_device) {
    usb_virtual_device_t* device = calloc(1, sizeof(usb_virtual_device_t));
    if (!device) {
        return ZX_ERR_NO_MEMORY;
    }
    device->bus = bus;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-virtual-device",
        .ctx = device,
        .ops = &usb_virtual_device_device_proto,
        .proto_id = ZX_PROTOCOL_USB_DCI,
        .proto_ops = &virt_device_dci_protocol,
    };

    zx_status_t status = device_add(device->bus->zxdev, &args, &device->zxdev);

    if (status != ZX_OK) {
        free(device);
        return status;
    }

    *out_device = device;
    return ZX_OK;
}

void usb_virtual_device_release(usb_virtual_device_t* device) {
    device_remove(device->zxdev);
}
