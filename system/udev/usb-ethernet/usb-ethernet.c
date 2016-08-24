// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb-device.h>
#include <system/listnode.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "asix.h"

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define INTR_REQ_COUNT 4
#define USB_BUF_SIZE 2048
#define INTR_REQ_SIZE 8
#define ETH_HEADER_SIZE 4

typedef struct {
    mx_device_t device;
    mx_device_t* usb_device;
    usb_device_protocol_t* device_protocol;
    mx_driver_t* driver;

    uint8_t phy_id;
    uint8_t mac_addr[6];
    uint8_t status[INTR_REQ_SIZE];
    bool online;

    usb_endpoint_t* bulk_in;
    usb_endpoint_t* bulk_out;
    usb_endpoint_t* intr_ep;

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
    // TODO (voydanoff) signal error state here
    mx_signals_t new_signals = 0;
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
    // FIXME what to do with error here?
    mtx_lock(&eth->mutex);
    list_add_tail(&eth->free_write_reqs, &request->node);
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
}

static void usb_ethernet_interrupt_complete(iotxn_t* request, void* cookie) {
    usb_ethernet_t* eth = (usb_ethernet_t*)cookie;
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
    mx_status_t status = NO_ERROR;

    mtx_lock(&eth->mutex);

    list_node_t* node = list_remove_head(&eth->free_write_reqs);
    if (!node) {
        status = ERR_NOT_ENOUGH_BUFFER;
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
        status = ERR_NOT_VALID;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
        goto out;
    }

    uint8_t header[ETH_HEADER_SIZE];
    request->ops->copyfrom(request, header, ETH_HEADER_SIZE, 0);
    uint16_t length1 = (header[0] | (uint16_t)header[1] << 8) & 0x7FF;
    uint16_t length2 = (~(header[2] | (uint16_t)header[3] << 8)) & 0x7FF;

    if (length1 != length2) {
        printf("invalid header: length1: %d length2: %d offset %ld\n", length1, length2, offset);
        status = ERR_NOT_VALID;
        offset = 0;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
        goto out;
    }
    if (length1 > length) {
        status = ERR_NOT_ENOUGH_BUFFER;
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

static mx_status_t usb_ethernet_release(mx_device_t* device) {
    usb_ethernet_t* eth = get_usb_ethernet(device);
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
        return ERR_NOT_ENOUGH_BUFFER;
    }
    return usb_ethernet_recv(dev, data, len);
}

static ssize_t eth_write(mx_device_t* dev, const void* data, size_t len, mx_off_t off) {
    return usb_ethernet_send(dev, data, len);
}

static mx_protocol_device_t usb_ethernet_device_proto = {
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
        return status;
    }

    // select the PHY
    uint8_t phy_addr[2];
    status = usb_control(eth->usb_device, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_PHY_ADDR, 0, 0, &phy_addr, sizeof(phy_addr));
    if (status < 0) {
        printf("ASIX_REQ_READ_PHY_ADDR failed\n");
        return status;
    }
    eth->phy_id = phy_addr[1];
    int embed_phy = (eth->phy_id & 0x1F) == 0x10 ? 1 : 0;
    status = usb_ethernet_set_value(eth, ASIX_REQ_SW_PHY_SELECT, embed_phy);
    if (status < 0) {
        printf("ASIX_REQ_SW_PHY_SELECT failed\n");
        return status;
    }

    // Reset
    status = usb_ethernet_set_value(eth, ASIX_REQ_SW_RESET, ASIX_RESET_PRL | ASIX_RESET_IPPD);
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        return status;
    }
    status = usb_ethernet_set_value(eth, ASIX_REQ_SW_RESET, 0);
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        return status;
    }
    status = usb_ethernet_set_value(eth, ASIX_REQ_SW_RESET,
                                    (embed_phy ? ASIX_RESET_IPRL : ASIX_RESET_PRTE));
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        return status;
    }
    status = usb_ethernet_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE, 0);
    if (status < 0) {
        printf("ASIX_REQ_RX_CONTROL_WRITE failed\n");
        return status;
    }

    status = usb_ethernet_wait_for_phy(eth);
    if (status < 0) {
        return status;
    }

    uint16_t medium = ASIX_MEDIUM_MODE_FD | ASIX_MEDIUM_MODE_AC | ASIX_MEDIUM_MODE_RFC | ASIX_MEDIUM_MODE_TFC | ASIX_MEDIUM_MODE_JFE | ASIX_MEDIUM_MODE_RE | ASIX_MEDIUM_MODE_PS;
    status = usb_ethernet_set_value(eth, ASIX_REQ_MEDIUM_MODE, medium);
    if (status < 0) {
        printf("ASIX_REQ_MEDIUM_MODE failed\n");
        return status;
    }

    status = usb_control(eth->usb_device, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_IPG_WRITE, ASIX_IPG_DEFAULT | (ASIX_IPG1_DEFAULT << 8),
                         ASIX_IPG2_DEFAULT, NULL, 0);
    if (status < 0) {
        printf("ASIX_REQ_IPG_WRITE failed\n");
        return status;
    }

    status = usb_ethernet_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE, ASIX_RX_CTRL_AMALL | ASIX_RX_CTRL_AB | ASIX_RX_CTRL_S0);
    if (status < 0) {
        printf("ASIX_REQ_RX_CONTROL_WRITE failed\n");
        return status;
    }

    status = usb_control(eth->usb_device, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_NODE_ID_READ, 0, 0, eth->mac_addr, sizeof(eth->mac_addr));
    if (status < 0) {
        printf("ASIX_REQ_NODE_ID_READ failed\n");
        return status;
    }
    printf("usb_ethernet MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           eth->mac_addr[0], eth->mac_addr[1], eth->mac_addr[2],
           eth->mac_addr[3], eth->mac_addr[4], eth->mac_addr[5]);

    status = device_init(&eth->device, eth->driver, "usb-ethernet", &usb_ethernet_device_proto);
    if (status != NO_ERROR) {
        free(eth);
        return status;
    }

    mtx_lock(&eth->mutex);
    queue_interrupt_requests_locked(eth);
    mtx_unlock(&eth->mutex);

    eth->device.protocol_id = MX_PROTOCOL_ETHERNET;
    eth->device.protocol_ops = &usb_ethernet_proto;
    device_add(&eth->device, eth->usb_device);

    return NO_ERROR;
}

static mx_status_t usb_ethernet_bind(mx_driver_t* driver, mx_device_t* device) {
    usb_device_protocol_t* protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&protocol)) {
        return ERR_NOT_SUPPORTED;
    }
    usb_device_config_t* device_config;
    mx_status_t status = protocol->get_config(device, &device_config);
    if (status < 0)
        return status;

    // find our endpoints
    usb_configuration_t* config = &device_config->configurations[0];
    usb_interface_t* intf = &config->interfaces[0];
    if (intf->num_endpoints != 3) {
        printf("usb_ethernet_bind wrong number of endpoints: %d\n", intf->num_endpoints);
        return ERR_NOT_SUPPORTED;
    }
    usb_endpoint_t* bulk_in = NULL;
    usb_endpoint_t* bulk_out = NULL;
    usb_endpoint_t* intr_ep = NULL;

    for (int i = 0; i < intf->num_endpoints; i++) {
        usb_endpoint_t* endp = &intf->endpoints[i];
        if (endp->direction == USB_ENDPOINT_OUT) {
            if (endp->type == USB_ENDPOINT_BULK) {
                bulk_out = endp;
            }
        } else {
            if (endp->type == USB_ENDPOINT_BULK) {
                bulk_in = endp;
            } else if (endp->type == USB_ENDPOINT_INTERRUPT) {
                intr_ep = endp;
            }
        }
    }
    if (!bulk_in || !bulk_out || !intr_ep) {
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
    eth->device_protocol = protocol;
    eth->bulk_in = bulk_in;
    eth->bulk_out = bulk_out;
    eth->intr_ep = intr_ep;

    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_in->descriptor, USB_BUF_SIZE, 0);
        if (!req)
            return ERR_NO_MEMORY;
        req->length = USB_BUF_SIZE;
        req->complete_cb = usb_ethernet_read_complete;
        req->cookie = eth;
        list_add_head(&eth->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_out->descriptor, USB_BUF_SIZE, 0);
        if (!req)
            return ERR_NO_MEMORY;
        req->length = USB_BUF_SIZE;
        req->complete_cb = usb_ethernet_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }
    for (int i = 0; i < INTR_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(intr_ep->descriptor, INTR_REQ_SIZE, 0);
        if (!req)
            return ERR_NO_MEMORY;
        req->length = INTR_REQ_SIZE;
        req->complete_cb = usb_ethernet_interrupt_complete;
        req->cookie = eth;
        list_add_head(&eth->free_intr_reqs, &req->node);
    }

    thrd_t thread;
    thrd_create_with_name(&thread, usb_ethernet_start_thread, eth, "usb_ethernet_start_thread");
    thrd_detach(thread);

    return NO_ERROR;
}

static mx_status_t usb_ethernet_unbind(mx_driver_t* drv, mx_device_t* dev) {
    // TODO - cleanup
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_DEVICE),
    BI_ABORT_IF(NE, BIND_USB_VID, ASIX_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, ASIX_PID),
};

mx_driver_t _driver_usb_ethernet BUILTIN_DRIVER = {
    .name = "usb_ethernet",
    .ops = {
        .bind = usb_ethernet_bind,
        .unbind = usb_ethernet_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
