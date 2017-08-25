// Copyright 2017 The Fuchsia Authors. All riusghts reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-dci.h>

#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "usb-virtual-bus.h"

typedef struct usb_virtual_device {
    mx_device_t* mxdev;
    usb_virtual_bus_t* bus;
    usb_dci_interface_t dci_intf;
} usb_virtual_device_t;

void usb_virtual_device_control(usb_virtual_device_t* device, iotxn_t* txn) {
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    usb_setup_t* setup = &data->setup;
    mx_status_t status;
    size_t length = le16toh(setup->wLength);
    size_t actual = 0;

    printf("usb_virtual_device_control type: 0x%02X req: %d value: %d index: %d length: %zu\n",
           setup->bmRequestType, setup->bRequest, le16toh(setup->wValue), le16toh(setup->wIndex),
           length);

    if (device->dci_intf.ops) {
        void* buffer = NULL;

        if (length > 0) {
            iotxn_mmap(txn, &buffer);
        }

        status = usb_dci_control(&device->dci_intf, setup, buffer, length, &actual);
    } else {
        status = MX_ERR_UNAVAILABLE;
    }

    iotxn_complete(txn, status, actual);
}

static mx_status_t device_set_interface(void* ctx, usb_dci_interface_t* dci_intf) {
    usb_virtual_device_t* device = ctx;
    memcpy(&device->dci_intf, dci_intf, sizeof(device->dci_intf));
    return MX_OK;
}

static mx_status_t device_config_ep(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                    usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return MX_OK;
}

static mx_status_t device_disable_ep(void* ctx, uint8_t ep_addr) {
    return MX_OK;
}

static mx_status_t device_set_enabled(void* ctx, bool enabled) {
    usb_virtual_device_t* device = ctx;
    return usb_virtual_bus_set_device_enabled(device->bus, enabled);
}

static mx_status_t device_ep_set_stall(void* ctx, uint8_t ep_address) {
    usb_virtual_device_t* device = ctx;
    return usb_virtual_bus_set_stall(device->bus, ep_address, true);
}

static mx_status_t device_ep_clear_stall(void* ctx, uint8_t ep_address) {
    usb_virtual_device_t* device = ctx;
    return usb_virtual_bus_set_stall(device->bus, ep_address, false);
}

usb_dci_protocol_ops_t virtual_device_protocol = {
    .set_interface = device_set_interface,
    .config_ep = device_config_ep,
    .disable_ep = device_disable_ep,
    .set_enabled = device_set_enabled,
    .ep_set_stall = device_ep_set_stall,
    .ep_clear_stall = device_ep_clear_stall,
};

static mx_status_t virt_device_open(void* ctx, mx_device_t** dev_out, uint32_t flags) {
printf("device_open\n");
    return MX_OK;
}

static void virt_device_iotxn_queue(void* ctx, iotxn_t* txn) {
    usb_virtual_device_t* device = ctx;
    iotxn_queue(device->bus->mxdev, txn);
}

static void virt_device_unbind(void* ctx) {
    printf("virt_device_unbind\n");
    usb_virtual_device_t* device = ctx;
    device_remove(device->mxdev);
}

static void virt_device_release(void* ctx) {
    usb_virtual_device_t* device = ctx;
    free(device);
}

static mx_protocol_device_t usb_virtual_device_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = virt_device_open,
    .iotxn_queue = virt_device_iotxn_queue,
    .unbind = virt_device_unbind,
    .release = virt_device_release,
};

mx_status_t usb_virtual_device_add(usb_virtual_bus_t* bus, usb_virtual_device_t** out_device) {
    usb_virtual_device_t* device = calloc(1, sizeof(usb_virtual_device_t));
    if (!device) {
        return MX_ERR_NO_MEMORY;
    }
    device->bus = bus;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-virtual-device",
        .ctx = device,
        .ops = &usb_virtual_device_device_proto,
        .proto_id = MX_PROTOCOL_USB_DCI,
        .proto_ops = &virtual_device_protocol,
    };

    mx_status_t status = device_add(device->bus->mxdev, &args, &device->mxdev);

    if (status != MX_OK) {
        free(device);
        return status;
    }

    *out_device = device;
    return MX_OK;
}

void usb_virtual_device_release(usb_virtual_device_t* device) {
    device_remove(device->mxdev);
}
