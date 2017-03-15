// Copyright 2017 The Fuchsia Authors. All riusghts reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>
#include <ddk/protocol/usb.h>

#include <magenta/listnode.h>
#include <magenta/types.h>
#include <sync/completion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "usb-virtual-bus.h"

#define CLIENT_SLOT_ID  0
#define CLIENT_HUB_ID   0
#define CLIENT_SPEED    USB_SPEED_HIGH

typedef struct usb_virtual_host {
    mx_device_t* mxdev;
    usb_virtual_bus_t* bus;
    usb_bus_interface_t bus_intf;

    mtx_t lock;
    completion_t completion;
    bool connected;
} usb_virtual_host_t;

static void virt_host_set_bus_interface(void* ctx, usb_bus_interface_t* bus_intf) {
    usb_virtual_host_t* host = ctx;

    if (bus_intf) {
        memcpy(&host->bus_intf, bus_intf, sizeof(host->bus_intf));

        mtx_lock(&host->lock);
        bool connected = host->connected;
        mtx_unlock(&host->lock);

        if (connected) {
            usb_bus_add_device(&host->bus_intf, CLIENT_SLOT_ID, CLIENT_HUB_ID, CLIENT_SPEED);
        }
    } else {
        memset(&host->bus_intf, 0, sizeof(host->bus_intf));
    }
}

static size_t virt_host_get_max_device_count(void* ctx) {
    return 1;
}

static mx_status_t virt_host_enable_ep(void* ctx, uint32_t device_id,
                                       usb_endpoint_descriptor_t* ep_desc,
                                       usb_ss_ep_comp_descriptor_t* ss_comp_desc, bool enable) {
    return MX_OK;
}

static uint64_t virt_host_get_frame(void* ctx) {
    return 0;
}

mx_status_t virt_host_config_hub(void* ctx, uint32_t device_id, usb_speed_t speed,
                                 usb_hub_descriptor_t* descriptor) {
    return MX_OK;
}

mx_status_t virt_host_hub_device_added(void* ctx, uint32_t hub_address, int port,
                                       usb_speed_t speed) {
    return MX_OK;
}

mx_status_t virt_host_hub_device_removed(void* ctx, uint32_t hub_address, int port) {
    return MX_OK;
}

mx_status_t virt_host_reset_endpoint(void* ctx, uint32_t device_id, uint8_t ep_address) {
    return MX_ERR_NOT_SUPPORTED;
}

size_t virt_host_get_max_transfer_size(void* ctx, uint32_t device_id, uint8_t ep_address) {
    return 65536;
}

static usb_hci_protocol_ops_t virtual_host_protocol = {
    .set_bus_interface = virt_host_set_bus_interface,
    .get_max_device_count = virt_host_get_max_device_count,
    .enable_endpoint = virt_host_enable_ep,
    .get_current_frame = virt_host_get_frame,
    .configure_hub = virt_host_config_hub,
    .hub_device_added = virt_host_hub_device_added,
    .hub_device_removed = virt_host_hub_device_removed,
    .reset_endpoint = virt_host_reset_endpoint,
    .get_max_transfer_size = virt_host_get_max_transfer_size,
};

static void virt_host_iotxn_queue(void* ctx, iotxn_t* txn) {
    usb_virtual_host_t* host = ctx;
    iotxn_queue(host->bus->mxdev, txn);
}

static void virt_host_unbind(void* ctx) {
    printf("virt_host_unbind\n");
    usb_virtual_host_t* host = ctx;

    device_remove(host->mxdev);
}

static void virt_host_release(void* ctx) {
 printf("host_release\n");
   usb_virtual_host_t* host = ctx;

    free(host);
}

static mx_protocol_device_t virt_host_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = virt_host_iotxn_queue,
    .unbind = virt_host_unbind,
    .release = virt_host_release,
};

mx_status_t usb_virtual_host_add(usb_virtual_bus_t* bus, usb_virtual_host_t** out_host) {
    usb_virtual_host_t* host = calloc(1, sizeof(usb_virtual_host_t));
    if (!host) {
        return MX_ERR_NO_MEMORY;
    }

    mtx_init(&host->lock, mtx_plain);
    completion_reset(&host->completion);
    host->bus = bus;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-virtual-host",
        .ctx = host,
        .ops = &virt_host_device_proto,
        .proto_id = MX_PROTOCOL_USB_HCI,
        .proto_ops = &virtual_host_protocol,
    };

    mx_status_t status = device_add(host->bus->mxdev, &args, &host->mxdev);
    if (status != MX_OK) {
        free(host);
        return status;
    }

    *out_host = host;
    return MX_OK;
}

void usb_virtual_host_release(usb_virtual_host_t* host) {
    device_remove(host->mxdev);
}

void usb_virtual_host_set_connected(usb_virtual_host_t* host, bool connected) {
    mtx_lock(&host->lock);
    bool connect = connected && !host->connected;
    bool disconnect = !connected && host->connected;
    host->connected = connected;
    mtx_unlock(&host->lock);

    if (host->bus_intf.ops) {
        if (connect) {
            usb_bus_add_device(&host->bus_intf, CLIENT_SLOT_ID, CLIENT_HUB_ID, CLIENT_SPEED);
        } else if (disconnect) {
            usb_bus_remove_device(&host->bus_intf, CLIENT_SLOT_ID);
        }
    }
}
