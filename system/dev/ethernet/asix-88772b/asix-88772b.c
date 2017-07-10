// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/ethernet.h>
#include <driver/usb.h>
#include <magenta/device/ethernet.h>
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
    mx_device_t* device;
    mx_device_t* usb_device;
    usb_protocol_t usb;

    uint8_t phy_id;
    uint8_t mac_addr[6];
    uint8_t status[INTR_REQ_SIZE];
    bool online;
    bool dead;

    // pool of free USB requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;
    list_node_t free_intr_reqs;

    // callback interface to attached ethernet layer
    ethmac_ifc_t* ifc;
    void* cookie;

    mtx_t mutex;
} ax88772b_t;

static mx_status_t ax88772b_set_value(ax88772b_t* eth, uint8_t request, uint16_t value) {
    return usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                       request, value, 0, NULL, 0, MX_TIME_INFINITE);
}

static mx_status_t ax88772b_mdio_read(ax88772b_t* eth, uint8_t offset, uint16_t* value) {

    mx_status_t status = ax88772b_set_value(eth, ASIX_REQ_SW_SERIAL_MGMT_CTRL, 0);
    if (status < 0) {
        printf("ASIX_REQ_SW_SERIAL_MGMT_CTRL failed\n");
        return status;
    }
    status = usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_PHY_READ, eth->phy_id, offset,
                         value, sizeof(*value), MX_TIME_INFINITE);
    if (status < 0) {
        printf("ASIX_REQ_PHY_READ failed\n");
        return status;
    }
    status = ax88772b_set_value(eth, ASIX_REQ_HW_SERIAL_MGMT_CTRL, 0);
    if (status < 0) {
        printf("ASIX_REQ_HW_SERIAL_MGMT_CTRL failed\n");
        return status;
    }

    return MX_OK;
}

static mx_status_t ax88772b_mdio_write(ax88772b_t* eth, uint8_t offset, uint16_t value) {

    mx_status_t status = ax88772b_set_value(eth, ASIX_REQ_SW_SERIAL_MGMT_CTRL, 0);
    if (status < 0) {
        printf("ASIX_REQ_SW_SERIAL_MGMT_CTRL failed\n");
        return status;
    }
    status = usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_PHY_WRITE, eth->phy_id, offset,
                         &value, sizeof(value), MX_TIME_INFINITE);
    if (status < 0) {
        printf("ASIX_REQ_PHY_READ failed\n");
        return status;
    }
    status = ax88772b_set_value(eth, ASIX_REQ_HW_SERIAL_MGMT_CTRL, 0);
    if (status < 0) {
        printf("ASIX_REQ_HW_SERIAL_MGMT_CTRL failed\n");
        return status;
    }

    return MX_OK;
}

static mx_status_t ax88772b_wait_for_phy(ax88772b_t* eth) {

    for (int i = 0; i < 100; i++) {
        uint16_t bmsr;
        mx_status_t status = ax88772b_mdio_read(eth, ASIX_PHY_BMSR, &bmsr);
        if (status < 0) {
            printf("ax88772b_mdio_read failed\n");
            return status;
        }
        if (bmsr)
            return MX_OK;
        usleep(50);
    }

    printf("ax88772b_wait_for_phy timeout\n");
    return MX_ERR_TIMED_OUT;
}

static void queue_interrupt_requests_locked(ax88772b_t* eth) {
    list_node_t* node;
    while ((node = list_remove_head(&eth->free_intr_reqs)) != NULL) {
        iotxn_t* req = containerof(node, iotxn_t, node);
        iotxn_queue(eth->usb_device, req);
    }
}

static void ax88772b_recv(ax88772b_t* eth, iotxn_t* request) {
    size_t len = request->actual;
    uint8_t* pkt;
    iotxn_mmap(request, (void**) &pkt);

    while (len > ETH_HEADER_SIZE) {
        uint16_t length1 = (pkt[0] | (uint16_t)pkt[1] << 8) & 0x7FF;
        uint16_t length2 = (~(pkt[2] | (uint16_t)pkt[3] << 8)) & 0x7FF;
        pkt += ETH_HEADER_SIZE;
        len -= ETH_HEADER_SIZE;

        if (length1 != length2) {
            printf("invalid header: length1: %d length2: %d\n", length1, length2);
            return;
        }

        if (length1 > len) {
            return;
        }

        eth->ifc->recv(eth->cookie, pkt, length1, 0);
        pkt += length1;
        len -= length1;

        // align to uint16
        if (length1 & 1) {
            if (len == 0) {
                return;
            }
            pkt++;
            len--;
        }
    }
}

static void ax88772b_read_complete(iotxn_t* request, void* cookie) {
    ax88772b_t* eth = (ax88772b_t*)cookie;

    if (request->status == MX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if ((request->status == MX_OK) && eth->ifc) {
        ax88772b_recv(eth, request);
    }

    if (eth->online) {
        iotxn_queue(eth->usb_device, request);
    } else {
        list_add_head(&eth->free_read_reqs, &request->node);
    }
    mtx_unlock(&eth->mutex);
}

static void ax88772b_write_complete(iotxn_t* request, void* cookie) {
    ax88772b_t* eth = (ax88772b_t*)cookie;

    if (request->status == MX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    list_add_tail(&eth->free_write_reqs, &request->node);
    mtx_unlock(&eth->mutex);
}

static void ax88772b_interrupt_complete(iotxn_t* request, void* cookie) {
    ax88772b_t* eth = (ax88772b_t*)cookie;

    if (request->status == MX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->status == MX_OK && request->actual == sizeof(eth->status)) {
        uint8_t status[INTR_REQ_SIZE];

        iotxn_copyfrom(request, status, sizeof(status), 0);
        if (memcmp(eth->status, status, sizeof(eth->status))) {
#if 0
            const uint8_t* b = status;
            printf("ax88772b status changed: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                   b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
#endif
            memcpy(eth->status, status, sizeof(eth->status));
            uint8_t bb = eth->status[2];
            bool online = (bb & 1) != 0;
            bool was_online = eth->online;
            eth->online = online;
            if (online && !was_online) {
                if (eth->ifc) {
                    eth->ifc->status(eth->cookie, ETH_STATUS_ONLINE);
                }

                // Now that we are online, queue all our read requests
                iotxn_t* req;
                iotxn_t* prev;
                list_for_every_entry_safe (&eth->free_read_reqs, req, prev, iotxn_t, node) {
                    list_delete(&req->node);
                    iotxn_queue(eth->usb_device, req);
                }
            } else if (!online && was_online) {
                if (eth->ifc) {
                    eth->ifc->status(eth->cookie, 0);
                }
            }
        }
    }

    list_add_head(&eth->free_intr_reqs, &request->node);
    queue_interrupt_requests_locked(eth);

    mtx_unlock(&eth->mutex);
}

static mx_status_t _ax88772b_send(void* ctx, const void* buffer, size_t length) {
    ax88772b_t* eth = ctx;

    if (eth->dead) {
        return MX_ERR_PEER_CLOSED;
    }

    mx_status_t status = MX_OK;

    mtx_lock(&eth->mutex);

    list_node_t* node = list_remove_head(&eth->free_write_reqs);
    if (!node) {
        //TODO: block
        status = MX_ERR_BUFFER_TOO_SMALL;
        goto out;
    }
    iotxn_t* request = containerof(node, iotxn_t, node);

    if (length + ETH_HEADER_SIZE > USB_BUF_SIZE) {
        status = MX_ERR_INVALID_ARGS;
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

    iotxn_copyto(request, header, ETH_HEADER_SIZE, 0);
    iotxn_copyto(request, buffer, length, ETH_HEADER_SIZE);
    request->length = length + ETH_HEADER_SIZE;
    iotxn_queue(eth->usb_device, request);

out:
    mtx_unlock(&eth->mutex);
    return status;
}

static void ax88772b_unbind(void* ctx) {
    ax88772b_t* eth = ctx;

    mtx_lock(&eth->mutex);
    eth->dead = true;
    mtx_unlock(&eth->mutex);

    // this must be last since this can trigger releasing the device
    device_remove(eth->device);
}

static void ax88772b_free(ax88772b_t* eth) {
    iotxn_t* txn;
    while ((txn = list_remove_head_type(&eth->free_read_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_intr_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    free(eth);
}

static void ax88772b_release(void* ctx) {
    ax88772b_t* eth = ctx;
    ax88772b_free(eth);
}

static mx_protocol_device_t ax88772b_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = ax88772b_unbind,
    .release = ax88772b_release,
};

static mx_status_t ax88772b_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    ax88772b_t* eth = ctx;

    if (options) {
        return MX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    info->mtu = USB_BUF_SIZE - ETH_HEADER_SIZE;
    memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));

    return MX_OK;
}

static void ax88772b_stop(void* ctx) {
    ax88772b_t* eth = ctx;
    mtx_lock(&eth->mutex);
    eth->ifc = NULL;
    mtx_unlock(&eth->mutex);
}

static mx_status_t ax88772b_start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
    ax88772b_t* eth = ctx;
    mx_status_t status = MX_OK;

    mtx_lock(&eth->mutex);
    if (eth->ifc) {
        status = MX_ERR_BAD_STATE;
    } else {
        eth->ifc = ifc;
        eth->cookie = cookie;
        eth->ifc->status(eth->cookie, eth->online ? ETH_STATUS_ONLINE : 0);
    }
    mtx_unlock(&eth->mutex);

    return status;
}

static void ax88772b_send(void* ctx, uint32_t options, void* data, size_t length) {
    _ax88772b_send(ctx, data, length);
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = ax88772b_query,
    .stop = ax88772b_stop,
    .start = ax88772b_start,
    .send = ax88772b_send,
};

static int ax88772b_start_thread(void* arg) {
    ax88772b_t* eth = (ax88772b_t*)arg;

    // set some GPIOs
    mx_status_t status = ax88772b_set_value(eth, ASIX_REQ_GPIOS,
                                                ASIX_GPIO_GPO2EN | ASIX_GPIO_GPO_2 | ASIX_GPIO_RSE);
    if (status < 0) {
        printf("ASIX_REQ_WRITE_GPIOS failed\n");
        goto fail;
    }

    // select the PHY
    uint8_t phy_addr[2];
    status = usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_PHY_ADDR, 0, 0, &phy_addr, sizeof(phy_addr), MX_TIME_INFINITE);
    if (status < 0) {
        printf("ASIX_REQ_READ_PHY_ADDR failed\n");
        goto fail;
    }
    eth->phy_id = phy_addr[1];
    int embed_phy = (eth->phy_id & 0x1F) == 0x10 ? 1 : 0;
    status = ax88772b_set_value(eth, ASIX_REQ_SW_PHY_SELECT, embed_phy);
    if (status < 0) {
        printf("ASIX_REQ_SW_PHY_SELECT failed\n");
        goto fail;
    }

    // Reset
    status = ax88772b_set_value(eth, ASIX_REQ_SW_RESET, ASIX_RESET_PRL | ASIX_RESET_IPPD);
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        goto fail;
    }
    status = ax88772b_set_value(eth, ASIX_REQ_SW_RESET, 0);
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        goto fail;
    }
    status = ax88772b_set_value(eth, ASIX_REQ_SW_RESET,
                                    (embed_phy ? ASIX_RESET_IPRL : ASIX_RESET_PRTE));
    if (status < 0) {
        printf("ASIX_REQ_SW_RESET failed\n");
        goto fail;
    }
    status = ax88772b_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE, 0);
    if (status < 0) {
        printf("ASIX_REQ_RX_CONTROL_WRITE failed\n");
        goto fail;
    }

    status = ax88772b_wait_for_phy(eth);
    if (status < 0) {
        goto fail;
    }

    uint16_t medium = ASIX_MEDIUM_MODE_FD | ASIX_MEDIUM_MODE_AC | ASIX_MEDIUM_MODE_RFC | ASIX_MEDIUM_MODE_TFC | ASIX_MEDIUM_MODE_JFE | ASIX_MEDIUM_MODE_RE | ASIX_MEDIUM_MODE_PS;
    status = ax88772b_set_value(eth, ASIX_REQ_MEDIUM_MODE, medium);
    if (status < 0) {
        printf("ASIX_REQ_MEDIUM_MODE failed\n");
        goto fail;
    }

    status = usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_IPG_WRITE, ASIX_IPG_DEFAULT | (ASIX_IPG1_DEFAULT << 8),
                         ASIX_IPG2_DEFAULT, NULL, 0, MX_TIME_INFINITE);
    if (status < 0) {
        printf("ASIX_REQ_IPG_WRITE failed\n");
        goto fail;
    }

    status = ax88772b_set_value(eth, ASIX_REQ_RX_CONTROL_WRITE, ASIX_RX_CTRL_AMALL | ASIX_RX_CTRL_AB | ASIX_RX_CTRL_S0);
    if (status < 0) {
        printf("ASIX_REQ_RX_CONTROL_WRITE failed\n");
        goto fail;
    }

    status = usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                         ASIX_REQ_NODE_ID_READ, 0, 0, eth->mac_addr, sizeof(eth->mac_addr),
                         MX_TIME_INFINITE);
    if (status < 0) {
        printf("ASIX_REQ_NODE_ID_READ failed\n");
        goto fail;
    }
    printf("ax88772b MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           eth->mac_addr[0], eth->mac_addr[1], eth->mac_addr[2],
           eth->mac_addr[3], eth->mac_addr[4], eth->mac_addr[5]);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ax88772b",
        .ctx = eth,
        .ops = &ax88772b_device_proto,
        .proto_id = MX_PROTOCOL_ETHERMAC,
        .proto_ops = &ethmac_ops,
    };

    status = device_add(eth->usb_device, &args, &eth->device);
    if (status < 0) {
        printf("ax8872b: failed to create device: %d\n", status);
        goto fail;
    }

    mtx_lock(&eth->mutex);
    queue_interrupt_requests_locked(eth);
    mtx_unlock(&eth->mutex);
    return MX_OK;

fail:
    ax88772b_free(eth);
    return status;
}

static mx_status_t ax88772b_bind(void* ctx, mx_device_t* device, void** cookie) {
    usb_protocol_t usb;
    mx_status_t result = device_get_protocol(device, MX_PROTOCOL_USB, &usb);
    if (result != MX_OK) {
        return result;
    }

    // find our endpoints
    usb_desc_iter_t iter;
    result = usb_desc_iter_init(&usb, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf || intf->bNumEndpoints != 3) {
        usb_desc_iter_release(&iter);
        return MX_ERR_NOT_SUPPORTED;
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
        printf("ax88772b_bind could not find endpoints\n");
        return MX_ERR_NOT_SUPPORTED;
    }

    ax88772b_t* eth = calloc(1, sizeof(ax88772b_t));
    if (!eth) {
        printf("Not enough memory for ax88772b_t\n");
        return MX_ERR_NO_MEMORY;
    }

    list_initialize(&eth->free_read_reqs);
    list_initialize(&eth->free_write_reqs);
    list_initialize(&eth->free_intr_reqs);

    eth->usb_device = device;
    memcpy(&eth->usb, &usb, sizeof(eth->usb));

    mx_status_t status = MX_OK;
    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_in_addr, USB_BUF_SIZE);
        if (!req) {
            status = MX_ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = ax88772b_read_complete;
        req->cookie = eth;
        list_add_head(&eth->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_out_addr, USB_BUF_SIZE);
        if (!req) {
            status = MX_ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = ax88772b_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }
    for (int i = 0; i < INTR_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(intr_addr, INTR_REQ_SIZE);
        if (!req) {
            status = MX_ERR_NO_MEMORY;
            goto fail;
        }
        req->length = INTR_REQ_SIZE;
        req->complete_cb = ax88772b_interrupt_complete;
        req->cookie = eth;
        list_add_head(&eth->free_intr_reqs, &req->node);
    }

    thrd_t thread;
    thrd_create_with_name(&thread, ax88772b_start_thread, eth, "ax88772b_start_thread");
    thrd_detach(thread);

    return MX_OK;

fail:
    printf("ax88772b_bind failed: %d\n", status);
    ax88772b_free(eth);
    return status;
}

static mx_driver_ops_t ax88772b_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ax88772b_bind,
};

MAGENTA_DRIVER_BEGIN(ethernet_ax88772b, ax88772b_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, ASIX_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, ASIX_PID),
MAGENTA_DRIVER_END(ethernet_ax88772b)
