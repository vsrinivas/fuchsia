// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/ethernet.h>
#include <magenta/listnode.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "asix-88772b.h"

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define INTR_REQ_COUNT 4
#define USB_BUF_SIZE 2048
#define INTR_REQ_SIZE 8
#define ETH_HEADER_SIZE 4

typedef struct {
    mx_device_t device;
    mx_device_t* usb_device;
    mx_driver_t* driver;

    uint8_t phy_id;
    uint8_t mac_addr[6];
    uint8_t status[INTR_REQ_SIZE];
    bool online;
    bool dead;

    // pool of free USB requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;
    list_node_t free_intr_reqs;

    // list of received packets not yet read by upper layer
    list_node_t completed_reads;
    // offset of next packet to process from completed_reads head
    size_t read_offset;

    // the last signals we reported
    mx_signals_t signals;

    mtx_t mutex;
} usb_ethernet_t;
#define get_usb_ethernet(dev) containerof(dev, usb_ethernet_t, device)

static void update_signals_locked(usb_ethernet_t* eth) {
    mx_signals_t new_signals = 0;

    if (eth->dead)
        new_signals |= (DEV_STATE_READABLE | DEV_STATE_ERROR);
    if (!list_is_empty(&eth->completed_reads))
        new_signals |= DEV_STATE_READABLE;
    if (!list_is_empty(&eth->free_write_reqs) && eth->online)
        new_signals |= DEV_STATE_WRITABLE;
    if (new_signals != eth->signals) {
        device_state_set_clr(&eth->device, new_signals & ~eth->signals, eth->signals & ~new_signals);
        eth->signals = new_signals;
    }
}

static mx_status_t usb_ethernet_set_value(usb_ethernet_t* eth, uint8_t request, uint16_t value) {
    return usb_control(eth->usb_device, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                       request, value, 0, NULL, 0);
}

static mx_status_t usb_ethernet_mdio_read(usb_ethernet_t* eth, uint8_t offset, uint16_t* value) {

    mx_status_t status = usb_ethernet_set_value(eth, ASIX_REQ_SW_SERIAL_MGMT_CTRL, 0);
    if (status < 0) {
        printf("ASIX_REQ_SW_SERIAL_MGMT_CTRL failed\n");
        return status;
    }
    status = usb_control(eth->usb_device, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_PHY_READ, eth->phy_id, offset,
                         value, sizeof(*value));
    if (status < 0) {
        printf("ASIX_REQ_PHY_READ failed\n");
        return status;
    }
    status = usb_ethernet_set_value(eth, ASIX_REQ_HW_SERIAL_MGMT_CTRL, 0);
    if (status < 0) {
        printf("ASIX_REQ_HW_SERIAL_MGMT_CTRL failed\n");
        return status;
    }

    return NO_ERROR;
}

static mx_status_t usb_ethernet_mdio_write(usb_ethernet_t* eth, uint8_t offset, uint16_t value) {

    mx_status_t status = usb_ethernet_set_value(eth, ASIX_REQ_SW_SERIAL_MGMT_CTRL, 0);
    if (status < 0) {
        printf("ASIX_REQ_SW_SERIAL_MGMT_CTRL failed\n");
        return status;
    }
    status = usb_control(eth->usb_device, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_PHY_WRITE, eth->phy_id, offset,
                         &value, sizeof(value));
    if (status < 0) {
        printf("ASIX_REQ_PHY_READ failed\n");
        return status;
    }
    status = usb_ethernet_set_value(eth, ASIX_REQ_HW_SERIAL_MGMT_CTRL, 0);
    if (status < 0) {
        printf("ASIX_REQ_HW_SERIAL_MGMT_CTRL failed\n");
        return status;
    }

    return NO_ERROR;
}

static mx_status_t usb_ethernet_wait_for_phy(usb_ethernet_t* eth) {

    for (int i = 0; i < 100; i++) {
        uint16_t bmsr;
        mx_status_t status = usb_ethernet_mdio_read(eth, ASIX_PHY_BMSR, &bmsr);
        if (status < 0) {
            printf("usb_ethernet_mdio_read failed\n");
            return status;
        }
        if (bmsr)
            return NO_ERROR;
        usleep(50);
    }

    printf("usb_ethernet_wait_for_phy timeout\n");
    return ERR_TIMED_OUT;
}

static void requeue_read_request_locked(usb_ethernet_t* eth, iotxn_t* req) {
    if (eth->online) {
        iotxn_queue(eth->usb_device, req);
    }
}

static void queue_interrupt_requests_locked(usb_ethernet_t* eth) {
    list_node_t* node;
    while ((node = list_remove_head(&eth->free_intr_reqs)) != NULL) {
        iotxn_t* req = containerof(node, iotxn_t, node);
        iotxn_queue(eth->usb_device, req);
    }
}

static void usb_ethernet_read_complete(iotxn_t* request, void* cookie) {
    usb_ethernet_t* eth = (usb_ethernet_t*)cookie;

    if (request->status == ERR_REMOTE_CLOSED) {
        request->ops->release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->status == NO_ERROR) {
        list_add_tail(&eth->completed_reads, &request->node);
    } else {
        requeue_read_request_locked(eth, request);
    }
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
}

static void usb_ethernet_write_complete(iotxn_t* request, void* cookie) {
    usb_ethernet_t* eth = (usb_ethernet_t*)cookie;

    if (request->status == ERR_REMOTE_CLOSED) {
        request->ops->release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    list_add_tail(&eth->free_write_reqs, &request->node);
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
}

static void usb_ethernet_interrupt_complete(iotxn_t* request, void* cookie) {
    usb_ethernet_t* eth = (usb_ethernet_t*)cookie;

    if (request->status == ERR_REMOTE_CLOSED) {
        request->ops->release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->status == NO_ERROR && request->actual == sizeof(eth->status)) {
        uint8_t status[INTR_REQ_SIZE];

        request->ops->copyfrom(request, status, sizeof(status), 0);
        if (memcmp(eth->status, status, sizeof(eth->status))) {
#if 0
            const uint8_t* b = status;
            printf("usb_ethernet status changed: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                   b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
#endif
            memcpy(eth->status, status, sizeof(eth->status));
            uint8_t bb = eth->status[2];
            bool online = (bb & 1) != 0;
            bool was_online = eth->online;
            eth->online = online;
            if (online && !was_online) {
                // Now that we are online, queue all our read requests
                iotxn_t* req;
                iotxn_t* prev;
                list_for_every_entry_safe (&eth->free_read_reqs, req, prev, iotxn_t, node) {
                    list_delete(&req->node);
                    requeue_read_request_locked(eth, req);
                }
                update_signals_locked(eth);
            }
        }
    }

    list_add_head(&eth->free_intr_reqs, &request->node);
    queue_interrupt_requests_locked(eth);

    mtx_unlock(&eth->mutex);
}

mx_status_t usb_ethernet_send(mx_device_t* device, const void* buffer, size_t length) {
    usb_ethernet_t* eth = get_usb_ethernet(device);

    if (eth->dead) {
        return ERR_REMOTE_CLOSED;
    }

    mx_status_t status = NO_ERROR;

    mtx_lock(&eth->mutex);

    list_node_t* node = list_remove_head(&eth->free_write_reqs);
    if (!node) {
        status = ERR_BUFFER_TOO_SMALL;
        goto out;
    }
    iotxn_t* request = containerof(node, iotxn_t, node);

    if (length + ETH_HEADER_SIZE > USB_BUF_SIZE) {
        status = ERR_INVALID_ARGS;
        goto out;
    }

    // write 4 byte packet header
    uint8_t header[ETH_HEADER_SIZE];
    uint8_t lo = length & 0xFF;
    uint8_t hi = length >> 8;
    header[0] = lo;
    header[1] = hi;
    header[2] = lo ^ 0xFF;
    header[3] = hi ^ 0xFF;

    request->ops->copyto(request, header, ETH_HEADER_SIZE, 0);
    request->ops->copyto(request, buffer, length, ETH_HEADER_SIZE);
    request->length = length + ETH_HEADER_SIZE;
    iotxn_queue(eth->usb_device, request);

out:
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
    return status;
}

mx_status_t usb_ethernet_recv(mx_device_t* device, void* buffer, size_t length) {
    usb_ethernet_t* eth = get_usb_ethernet(device);

    if (eth->dead) {
        return ERR_REMOTE_CLOSED;
    }

    mx_status_t status = NO_ERROR;

    mtx_lock(&eth->mutex);
    size_t offset = eth->read_offset;

    list_node_t* node = list_peek_head(&eth->completed_reads);
    if (!node) {
        status = ERR_BAD_STATE;
        goto out;
    }
    iotxn_t* request = containerof(node, iotxn_t, node);
    int remaining = request->actual - offset;
    if (remaining < 4) {
        printf("usb_ethernet_recv short packet\n");
        status = ERR_INTERNAL;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
        goto out;
    }

    uint8_t header[ETH_HEADER_SIZE];
    request->ops->copyfrom(request, header, ETH_HEADER_SIZE, 0);
    uint16_t length1 = (header[0] | (uint16_t)header[1] << 8) & 0x7FF;
    uint16_t length2 = (~(header[2] | (uint16_t)header[3] << 8)) & 0x7FF;

    if (length1 != length2) {
        printf("invalid header: length1: %d length2: %d offset %zu\n", length1, length2, offset);
        status = ERR_INTERNAL;
        offset = 0;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
        goto out;
    }
    if (length1 > length) {
        status = ERR_BUFFER_TOO_SMALL;
        goto out;
    }
    request->ops->copyfrom(request, buffer, length1, ETH_HEADER_SIZE);
    status = length1;
    offset += (length1 + 4);
    if (offset & 1)
        offset++;
    if (offset >= request->actual) {
        offset = 0;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
    }

out:
    eth->read_offset = offset;

    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
    return status;
}

mx_status_t usb_ethernet_get_mac_addr(mx_device_t* device, uint8_t* out_addr) {
    usb_ethernet_t* eth = get_usb_ethernet(device);
    memcpy(out_addr, eth->mac_addr, sizeof(eth->mac_addr));
    return NO_ERROR;
}

bool usb_ethernet_is_online(mx_device_t* device) {
    usb_ethernet_t* eth = get_usb_ethernet(device);
    return eth->online;
}

size_t usb_ethernet_get_mtu(mx_device_t* device) {
    return USB_BUF_SIZE - ETH_HEADER_SIZE;
}

static ethernet_protocol_t usb_ethernet_proto = {
    .send = usb_ethernet_send,
    .recv = usb_ethernet_recv,
    .get_mac_addr = usb_ethernet_get_mac_addr,
    .is_online = usb_ethernet_is_online,
    .get_mtu = usb_ethernet_get_mtu,
};

static void usb_ethernet_unbind(mx_device_t* device) {
    usb_ethernet_t* eth = get_usb_ethernet(device);
    device_remove(&eth->device);

    mtx_lock(&eth->mutex);
    eth->dead = true;
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
}

static mx_status_t usb_ethernet_release(mx_device_t* device) {
    usb_ethernet_t* eth = get_usb_ethernet(device);

    iotxn_t* txn;
    while ((txn = list_remove_head_type(&eth->free_read_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_intr_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }

    free(eth);
    return NO_ERROR;
}

// simplified read/write interface

static ssize_t eth_read(mx_device_t* dev, void* data, size_t len, mx_off_t off) {
    // special case reading MAC address
    if (len == ETH_MAC_SIZE) {
        usb_ethernet_get_mac_addr(dev, data);
        return len;
    }
    if (len < usb_ethernet_get_mtu(dev)) {
        return ERR_BUFFER_TOO_SMALL;
    }
    return usb_ethernet_recv(dev, data, len);
}

static ssize_t eth_write(mx_device_t* dev, const void* data, size_t len, mx_off_t off) {
    return usb_ethernet_send(dev, data, len);
}

static mx_protocol_device_t usb_ethernet_device_proto = {
    .unbind = usb_ethernet_unbind,
    .release = usb_ethernet_release,
    .read = eth_read,
    .write = eth_write,
};

static int usb_ethernet_start_thread(void* arg) {
    usb_ethernet_t* eth = (usb_ethernet_t*)arg;

    // set some GPIOs
    mx_status_t status = usb_ethernet_set_value(eth, ASIX_REQ_GPIOS,
                                                ASIX_GPIO_GPO2EN | ASIX_GPIO_GPO_2 | ASIX_GPIO_RSE);
    if (status < 0) {
        printf("ASIX_REQ_WRITE_GPIOS failed\n");
        goto fail;
    }

    // select the PHY
    uint8_t phy_addr[2];
    status = usb_control(eth->usb_device, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_PHY_ADDR, 0, 0, &phy_addr, sizeof(phy_addr));
    if (status < 0) {
        printf("ASIX_REQ_READ_PHY_ADDR failed\n");
        goto fail;
    }
    eth->phy_id = phy_addr[1];
    int embed_phy = (eth->phy_id & 0x1F) == 0x10 ? 1 : 0;
    status = usb_ethernet_set_value(eth, ASIX_REQ_SW_PHY_SELECT, embed_phy);
    if (status < 0) {
        printf("ASIX_REQ_SW_PHY_SELECT failed\n");
        goto fail;
    }

    // Reset
    status = usb_ethernet_set_value(eth, ASIX_REQ_SW_RESET, ASIX_RESET_PRL | ASIX_RESET_IPPD);
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        goto fail;
    }
    status = usb_ethernet_set_value(eth, ASIX_REQ_SW_RESET, 0);
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        goto fail;
    }
    status = usb_ethernet_set_value(eth, ASIX_REQ_SW_RESET,
                                    (embed_phy ? ASIX_RESET_IPRL : ASIX_RESET_PRTE));
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        goto fail;
    }
    status = usb_ethernet_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE, 0);
    if (status < 0) {
        printf("ASIX_REQ_RX_CONTROL_WRITE failed\n");
        goto fail;
    }

    status = usb_ethernet_wait_for_phy(eth);
    if (status < 0) {
        goto fail;
    }

    uint16_t medium = ASIX_MEDIUM_MODE_FD | ASIX_MEDIUM_MODE_AC | ASIX_MEDIUM_MODE_RFC | ASIX_MEDIUM_MODE_TFC | ASIX_MEDIUM_MODE_JFE | ASIX_MEDIUM_MODE_RE | ASIX_MEDIUM_MODE_PS;
    status = usb_ethernet_set_value(eth, ASIX_REQ_MEDIUM_MODE, medium);
    if (status < 0) {
        printf("ASIX_REQ_MEDIUM_MODE failed\n");
        goto fail;
    }

    status = usb_control(eth->usb_device, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_IPG_WRITE, ASIX_IPG_DEFAULT | (ASIX_IPG1_DEFAULT << 8),
                         ASIX_IPG2_DEFAULT, NULL, 0);
    if (status < 0) {
        printf("ASIX_REQ_IPG_WRITE failed\n");
        goto fail;
    }

    status = usb_ethernet_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE, ASIX_RX_CTRL_AMALL | ASIX_RX_CTRL_AB | ASIX_RX_CTRL_S0);
    if (status < 0) {
        printf("ASIX_REQ_RX_CONTROL_WRITE failed\n");
        goto fail;
    }

    status = usb_control(eth->usb_device, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_NODE_ID_READ, 0, 0, eth->mac_addr, sizeof(eth->mac_addr));
    if (status < 0) {
        printf("ASIX_REQ_NODE_ID_READ failed\n");
        goto fail;
    }
    printf("usb_ethernet MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           eth->mac_addr[0], eth->mac_addr[1], eth->mac_addr[2],
           eth->mac_addr[3], eth->mac_addr[4], eth->mac_addr[5]);

    device_init(&eth->device, eth->driver, "usb-ethernet", &usb_ethernet_device_proto);

    mtx_lock(&eth->mutex);
    queue_interrupt_requests_locked(eth);
    mtx_unlock(&eth->mutex);

    eth->device.protocol_id = MX_PROTOCOL_ETHERNET;
    eth->device.protocol_ops = &usb_ethernet_proto;
    status = device_add(&eth->device, eth->usb_device);
    if (status == NO_ERROR) return NO_ERROR;

fail:
    usb_ethernet_release(&eth->device);
    return status;
}

static mx_status_t usb_ethernet_bind(mx_driver_t* driver, mx_device_t* device) {
    // find our endpoints
    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(device, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf || intf->bNumEndpoints != 3) {
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
    }

    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;
    uint8_t intr_addr = 0;

   usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    while (endp) {
        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_out_addr = endp->bEndpointAddress;
            }
        } else {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_in_addr = endp->bEndpointAddress;
            } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
                intr_addr = endp->bEndpointAddress;
            }
        }
        endp = usb_desc_iter_next_endpoint(&iter);
    }
    usb_desc_iter_release(&iter);

    if (!bulk_in_addr || !bulk_out_addr || !intr_addr) {
        printf("usb_ethernet_bind could not find endpoints\n");
        return ERR_NOT_SUPPORTED;
    }

    usb_ethernet_t* eth = calloc(1, sizeof(usb_ethernet_t));
    if (!eth) {
        printf("Not enough memory for usb_ethernet_t\n");
        return ERR_NO_MEMORY;
    }

    list_initialize(&eth->free_read_reqs);
    list_initialize(&eth->free_write_reqs);
    list_initialize(&eth->free_intr_reqs);
    list_initialize(&eth->completed_reads);

    eth->usb_device = device;
    eth->driver = driver;

    mx_status_t status = NO_ERROR;
    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_in_addr, USB_BUF_SIZE, 0);
        if (!req) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = usb_ethernet_read_complete;
        req->cookie = eth;
        list_add_head(&eth->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_out_addr, USB_BUF_SIZE, 0);
        if (!req) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = usb_ethernet_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }
    for (int i = 0; i < INTR_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(intr_addr, INTR_REQ_SIZE, 0);
        if (!req) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        req->length = INTR_REQ_SIZE;
        req->complete_cb = usb_ethernet_interrupt_complete;
        req->cookie = eth;
        list_add_head(&eth->free_intr_reqs, &req->node);
    }

    thrd_t thread;
    thrd_create_with_name(&thread, usb_ethernet_start_thread, eth, "usb_ethernet_start_thread");
    thrd_detach(thread);

    return NO_ERROR;

fail:
    printf("usb_ethernet_bind failed: %d\n", status);
    usb_ethernet_release(&eth->device);
    return status;
}

mx_driver_t _driver_usb_ethernet = {
    .ops = {
        .bind = usb_ethernet_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_usb_ethernet, "usb-ethernet-ax88772b", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, ASIX_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, ASIX_PID),
MAGENTA_DRIVER_END(_driver_usb_ethernet)
