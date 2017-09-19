// Copyright 2017 The Fuchsia Authors. All riusghts reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-dci.h>
#include <ddk/protocol/usb-function.h>
#include <zircon/device/usb-virt-bus.h>
#include <stdlib.h>
#include <stdio.h>

#include "usb-virtual-bus.h"

// for mapping bEndpointAddress value to/from index in range 0 - 31
// OUT endpoints are in range 1 - 15, IN endpoints are in range 17 - 31
#define ep_address_to_index(addr) (((addr) & 0xF) | (((addr) & 0x80) >> 3))
#define ep_index_to_address(index) (((index) & 0xF) | (((index) & 0x10) << 3))
#define OUT_EP_START    1
#define OUT_EP_END      15
#define IN_EP_START     17
#define IN_EP_END       31

static int usb_virtual_bus_thread(void* arg) {
    usb_virtual_bus_t* bus = arg;

    // FIXME how to exit this thread
    while (1) {
        completion_wait(&bus->completion, ZX_TIME_INFINITE);
        completion_reset(&bus->completion);

        mtx_lock(&bus->lock);

        // special case endpoint zero
        iotxn_t* txn = list_remove_head_type(&bus->eps[0].host_txns, iotxn_t, node);
        if (txn) {
            usb_virtual_device_control(bus->device, txn);
        }

        for (unsigned i = 1; i < USB_MAX_EPS; i++) {
            usb_virtual_ep_t* ep = &bus->eps[i];
            bool out = (i < IN_EP_START);

            while ((txn = list_peek_head_type(&ep->host_txns, iotxn_t, node)) != NULL) {
                iotxn_t* device_txn = list_remove_head_type(&ep->device_txns, iotxn_t, node);

                if (device_txn) {
                    zx_off_t offset = ep->txn_offset;
                    size_t length = txn->length - offset;
                    if (length > device_txn->length) {
                        length = device_txn->length;
                    }

                    void* device_buffer;
                    iotxn_mmap(device_txn, &device_buffer);

                    if (out) {
                        iotxn_copyfrom(txn, device_buffer, length, offset);
                    } else {
                        iotxn_copyto(txn, device_buffer, length, offset);
                    }
                    iotxn_complete(device_txn, ZX_OK, length);

                    offset += length;
                    if (offset < txn->length) {
                        ep->txn_offset = offset;
                    } else {
                        list_delete(&txn->node);
                        iotxn_complete(txn, ZX_OK, length);
                        ep->txn_offset = 0;
                    }
                } else {
                    break;
                }
            }
        }

        mtx_unlock(&bus->lock);
    }
    return 0;
}

zx_status_t usb_virtual_bus_set_mode(usb_virtual_bus_t* bus, usb_mode_t mode) {
    mtx_lock(&bus->lock);
    bool old_connect = bus->usb_mode == USB_MODE_DEVICE && bus->connected;
    bus->usb_mode = mode;
    bool connect = bus->usb_mode == USB_MODE_DEVICE && bus->connected;
    mtx_unlock(&bus->lock);

    if (connect != old_connect) {
        usb_virtual_host_set_connected(bus->host, connect);
    }

    return ZX_OK;
}

zx_status_t usb_virtual_bus_set_stall(usb_virtual_bus_t* bus, uint8_t ep_address, bool stall) {
    uint8_t index = ep_address_to_index(ep_address);
    if (index >= USB_MAX_EPS) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&bus->lock);
    usb_virtual_ep_t* ep = &bus->eps[index];
    ep->stalled = stall;

    iotxn_t* txn = NULL;
    if (stall) {
        txn = list_remove_head_type(&ep->host_txns, iotxn_t, node);
    }
    mtx_unlock(&bus->lock);

    if (txn) {
        iotxn_complete(txn, ZX_ERR_IO_REFUSED, 0);
    }

    return ZX_OK;
}

static void usb_bus_iotxn_queue(void* ctx, iotxn_t* txn) {
    usb_virtual_bus_t* bus = ctx;

    if (txn->protocol == ZX_PROTOCOL_USB) {
        usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
        uint8_t index = ep_address_to_index(data->ep_address);
        if (index >= USB_MAX_EPS) {
            printf("usb_bus_iotxn_queue bad endpoint %u\n", data->ep_address);
            iotxn_complete(txn, ZX_ERR_INVALID_ARGS, 0);
            return;
        }
        usb_virtual_ep_t* ep = &bus->eps[index];

        if (ep->stalled) {
            iotxn_complete(txn, ZX_ERR_IO_REFUSED, 0);
            return;
        }
        list_add_tail(&ep->host_txns, &txn->node);
        completion_signal(&bus->completion);
    } else if (txn->protocol == ZX_PROTOCOL_USB_FUNCTION) {
        usb_function_protocol_data_t* data = iotxn_pdata(txn, usb_function_protocol_data_t);
        uint8_t index = ep_address_to_index(data->ep_address);
        if (index == 0 || index >= USB_MAX_EPS) {
            printf("usb_bus_iotxn_queue bad endpoint %u\n", data->ep_address);
            iotxn_complete(txn, ZX_ERR_INVALID_ARGS, 0);
            return;
        }
        usb_virtual_ep_t* ep = &bus->eps[index];

        list_add_tail(&ep->device_txns, &txn->node);
        completion_signal(&bus->completion);
    } else {
        printf("usb_bus_iotxn_queue bad protocol 0x%x\n", txn->protocol);
        iotxn_complete(txn, ZX_ERR_INVALID_ARGS, 0);
    }
}

static zx_status_t usb_bus_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len, size_t* out_actual) {
    usb_virtual_bus_t* bus = ctx;
    zx_status_t status;

    if (op == IOCTL_USB_VIRT_BUS_ENABLE) {
        if (!in_buf || in_len != sizeof(int)) {
            return ZX_ERR_INVALID_ARGS;
        }
        bool enable = !!*((int *)in_buf);

        if (enable) {
            if (!bus->host) {
                status = usb_virtual_host_add(bus, &bus->host);
                if (status != ZX_OK) {
                    return status;
                }
            }
            if (!bus->device) {
                status = usb_virtual_device_add(bus, &bus->device);
                if (status != ZX_OK) {
                    return status;
                }
            }
        } else {
            if (bus->host) {
                usb_virtual_host_release(bus->host);
                bus->host = NULL;
            }
            if (bus->device) {
                usb_virtual_device_release(bus->device);
                bus->device = NULL;
            }
        }

        return ZX_OK;
    } else if (op == IOCTL_USB_VIRT_BUS_SET_CONNECTED) {
        if (!in_buf || in_len != sizeof(int)) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (!bus->host || !bus->device) {
            return ZX_ERR_BAD_STATE;
        }
        int connected = *((int *)in_buf);

        mtx_lock(&bus->lock);
        bool old_connect = bus->usb_mode == USB_MODE_DEVICE && bus->connected;
        bus->connected = !!connected;
        bool connect = bus->usb_mode == USB_MODE_DEVICE && bus->connected;
        mtx_unlock(&bus->lock);

        if (connect != old_connect) {
            usb_virtual_host_set_connected(bus->host, connect);
        }

        return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

static void usb_bus_unbind(void* ctx) {
    usb_virtual_bus_t* bus = ctx;
    device_remove(bus->zxdev);
}

static void usb_bus_release(void* ctx) {
    usb_virtual_bus_t* bus = ctx;
    free(bus);
}

static zx_protocol_device_t usb_virtual_bus_proto = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = usb_bus_iotxn_queue,
    .ioctl = usb_bus_ioctl,
    .unbind = usb_bus_unbind,
    .release = usb_bus_release,
};

static zx_status_t usb_virtual_bus_bind(void* ctx, zx_device_t* parent, void** cookie) {
printf("usb_virtual_bus_bind\n");
    usb_virtual_bus_t* bus = calloc(1, sizeof(usb_virtual_bus_t));
    if (!bus) {
        return ZX_ERR_NO_MEMORY;
    }

    for (unsigned i = 0; i < USB_MAX_EPS; i++) {
        usb_virtual_ep_t* ep = &bus->eps[i];
        list_initialize(&ep->host_txns);
        list_initialize(&ep->device_txns);
    }
    mtx_init(&bus->lock, mtx_plain);
    completion_reset(&bus->completion);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-virtual-bus",
        .ctx = bus,
        .ops = &usb_virtual_bus_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    zx_status_t status = device_add(parent, &args, &bus->zxdev);
    if (status != ZX_OK) {
        free(bus);
        return status;
    }

    thrd_t thread;
    thrd_create_with_name(&thread, usb_virtual_bus_thread, bus, "usb-virtual-bus-thread");
    thrd_detach(thread);


    return ZX_OK;
}

static zx_driver_ops_t bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_virtual_bus_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_virtual_bus, bus_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(usb_virtual_bus)
