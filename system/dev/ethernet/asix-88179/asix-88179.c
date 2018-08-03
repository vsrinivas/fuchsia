// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb/usb.h>
#include <lib/cksum.h>
#include <pretty/hexdump.h>
#include <lib/sync/completion.h>
#include <zircon/assert.h>
#include <zircon/device/ethernet.h>
#include <zircon/listnode.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "asix-88179.h"

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 8
#define USB_BUF_SIZE 24576
#define MAX_TX_IN_FLIGHT 4
#define INTR_REQ_SIZE 8
#define RX_HEADER_SIZE 4
#define AX88179_MTU 1500
#define MAX_ETH_HDRS 26
#define MAX_MULTICAST_FILTER_ADDRS 32
#define MULTICAST_FILTER_NBYTES 8

/*
 * The constants are determined based on Pluggable gigabit Ethernet adapter(Model: USBC-E1000),
 * connected on pixelbook. At times, the device returns NRDY token when it is unable to match the
 * pace of the client driver but does not recover by sending a ERDY token within the controller's
 * time limit. ETHMAC_INITIAL_TRANSMIT_DELAY helps us avoids getting into this situation by adding
 * a delay at the beginning.
 */
#define ETHMAC_MAX_TRANSMIT_DELAY 100
#define ETHMAC_MAX_RECV_DELAY 100
#define ETHMAC_TRANSMIT_DELAY 10
#define ETHMAC_RECV_DELAY 10
#define ETHMAC_INITIAL_TRANSMIT_DELAY 15
#define ETHMAC_INITIAL_RECV_DELAY 0

typedef struct {
    zx_device_t* device;
    zx_device_t* usb_device;
    usb_protocol_t usb;

    uint8_t mac_addr[ETH_MAC_SIZE];
    uint8_t status[INTR_REQ_SIZE];
    bool online;
    bool multicast_filter_overflow;
    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;

    // interrupt in request
    usb_request_t* interrupt_req;
    sync_completion_t completion;

    // pool of free USB bulk requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;

    // Locks the usb_tx_in_flight, pending_usb_tx, and pending_netbuf lists.
    mtx_t tx_lock;
    // Whether a request has been queued to the USB device.
    uint8_t usb_tx_in_flight;
    // List of requests that have pending data. Used to buffer data if a USB transaction is in
    // flight. Additional data must be appended to the tail of the list, or if that's full, a
    // request from free_write_reqs must be added to the list.
    list_node_t pending_usb_tx;
    // List of netbufs that haven't been copied into a USB transaction yet. Should only contain
    // entries if all allocated USB transactions are full.
    list_node_t pending_netbuf;

    uint64_t rx_endpoint_delay;    // wait time between 2 recv requests
    uint64_t tx_endpoint_delay;    // wait time between 2 transmit requests
    // callback interface to attached ethernet layer
    ethmac_ifc_t* ifc;
    void* cookie;

    thrd_t thread;
    mtx_t mutex;
} ax88179_t;

typedef struct {
    uint16_t num_pkts;
    uint16_t pkt_hdr_off;
} ax88179_rx_hdr_t;

typedef struct {
    uint16_t tx_len;
    uint16_t unused[3];
    // TODO: support additional tx header fields
} ax88179_tx_hdr_t;

static zx_status_t ax88179_read_mac(ax88179_t* eth, uint8_t reg_addr, uint8_t reg_len, void* data) {
    size_t out_length;
    zx_status_t status = usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                     AX88179_REQ_MAC, reg_addr, reg_len, data, reg_len,
                                     ZX_TIME_INFINITE, &out_length);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        zxlogf(SPEW, "read mac %#x:\n", reg_addr);
        if (status == ZX_OK) {
            hexdump8(data, out_length);
        }
    }
    return status;
}

static zx_status_t ax88179_write_mac(ax88179_t* eth, uint8_t reg_addr, uint8_t reg_len, void* data) {
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        zxlogf(SPEW, "write mac %#x:\n", reg_addr);
        hexdump8(data, reg_len);
    }
    return usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_REQ_MAC,
                       reg_addr, reg_len, data, reg_len, ZX_TIME_INFINITE, NULL);
}

static zx_status_t ax88179_read_phy(ax88179_t* eth, uint8_t reg_addr, uint16_t* data) {
    size_t out_length;
    zx_status_t status = usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                     AX88179_REQ_PHY, AX88179_PHY_ID, reg_addr, data, sizeof(*data),
                                     ZX_TIME_INFINITE, &out_length);
    if (out_length == sizeof(*data)) {
        zxlogf(SPEW, "read phy %#x: %#x\n", reg_addr, *data);
    }
    return status;
}

static zx_status_t ax88179_write_phy(ax88179_t* eth, uint8_t reg_addr, uint16_t data) {
    zxlogf(SPEW, "write phy %#x: %#x\n", reg_addr, data);
    return usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_REQ_PHY,
                       AX88179_PHY_ID, reg_addr, &data, sizeof(data), ZX_TIME_INFINITE, NULL);
}

static uint8_t ax88179_media_mode[6][2] = {
    { 0x30, 0x01 }, // 10 Mbps, half-duplex
    { 0x32, 0x01 }, // 10 Mbps, full-duplex
    { 0x30, 0x03 }, // 100 Mbps, half-duplex
    { 0x32, 0x03 }, // 100 Mbps, full-duplex
    { 0, 0 },       // unused
    { 0x33, 0x01 }, // 1000Mbps, full-duplex
};

// The array indices here correspond to the bit positions in the AX88179 MAC
// PLSR register.
static uint8_t ax88179_bulk_in_config[5][5][5] = {
    { { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, },
    { // Full Speed
        { 0 },
        { 0x07, 0xcc, 0x4c, 0x18, 0x08 },  // 10 Mbps
        { 0x07, 0xcc, 0x4c, 0x18, 0x08 },  // 100 Mbps
        { 0 },
        { 0x07, 0xcc, 0x4c, 0x18, 0x08 },  // 1000 Mbps
    },
    { // High Speed
        { 0 },
        { 0x07, 0xcc, 0x4c, 0x18, 0x08 },  // 10 Mbps
        { 0x07, 0xae, 0x07, 0x18, 0xff },  // 100 Mbps
        { 0 },
        { 0x07, 0x20, 0x03, 0x16, 0xff },  // 1000 Mbps
    },
    { { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, },
    { // Super Speed
        { 0 },
        { 0x07, 0xcc, 0x4c, 0x18, 0x08 },  // 10 Mbps
        { 0x07, 0xae, 0x07, 0x18, 0xff },  // 100 Mbps
        { 0 },
        { 0x07, 0x4f, 0x00, 0x12, 0xff },  // 1000 Mbps
    },
};

static zx_status_t ax88179_configure_bulk_in(ax88179_t* eth, uint8_t plsr) {
    uint8_t usb_mode = plsr & AX88179_PLSR_USB_MASK;
    if (usb_mode & (usb_mode-1)) {
        zxlogf(ERROR, "ax88179: invalid usb mode: %#x\n", usb_mode);
        return ZX_ERR_INVALID_ARGS;
    }

    uint8_t speed = plsr & AX88179_PLSR_EPHY_MASK;
    if (speed & (speed-1)) {
        zxlogf(ERROR, "ax88179: invalid eth speed: %#x\n", speed);
    }

    zx_status_t status = ax88179_write_mac(eth, AX88179_MAC_RQCR, 5,
            ax88179_bulk_in_config[usb_mode][speed >> 4]);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RQCR, status);
    }
    return status;
}

static zx_status_t ax88179_configure_medium_mode(ax88179_t* eth) {
    uint16_t data = 0;
    zx_status_t status = ax88179_read_phy(eth, AX88179_PHY_PHYSR, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_read_phy to %#x failed: %d\n", AX88179_PHY_PHYSR, status);
        return status;
    }

    unsigned int mode = (data & (AX88179_PHYSR_SPEED|AX88179_PHYSR_DUPLEX)) >> 13;
    zxlogf(TRACE, "ax88179 medium mode: %#x\n", mode);
    if (mode == 4 || mode > 5) {
        zxlogf(ERROR, "ax88179 mode invalid (mode=%u)\n", mode);
        return ZX_ERR_NOT_SUPPORTED;
    }
    status = ax88179_write_mac(eth, AX88179_MAC_MSR, 2, ax88179_media_mode[mode]);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_MSR, status);
        return status;
    }

    data = 0;
    status = ax88179_read_mac(eth, AX88179_MAC_PLSR, 1, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_read_mac to %#x failed: %d\n", AX88179_MAC_PLSR, status);
        return status;
    }
    status = ax88179_configure_bulk_in(eth, data & 0xff);

    return status;
}

static zx_status_t ax88179_recv(ax88179_t* eth, usb_request_t* request) {
    zxlogf(SPEW, "request len %" PRIu64"\n", request->response.actual);

    if (request->response.actual < 4) {
        zxlogf(ERROR, "ax88179_recv short packet\n");
        return ZX_ERR_INTERNAL;
    }

    uint8_t* read_data;
    zx_status_t status = usb_req_mmap(&eth->usb, request, (void*)&read_data);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_req_mmap failed: %d\n", status);
        return status;
    }

    ptrdiff_t rxhdr_off = request->response.actual - sizeof(ax88179_rx_hdr_t);
    ax88179_rx_hdr_t* rxhdr = (ax88179_rx_hdr_t*)(read_data + rxhdr_off);
    zxlogf(SPEW, "rxhdr offset %u, num %u\n", rxhdr->pkt_hdr_off, rxhdr->num_pkts);
    if (rxhdr->num_pkts < 1 || rxhdr->pkt_hdr_off >= rxhdr_off) {
        zxlogf(ERROR, "%s bad packet\n", __func__);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    size_t offset = 0;
    size_t packet = 0;

    while (packet < rxhdr->num_pkts) {
        zxlogf(SPEW, "next packet: %zd\n", packet);
        ptrdiff_t pkt_idx = packet++ * sizeof(uint32_t);
        uint32_t* pkt_hdr = (uint32_t*)(read_data + rxhdr->pkt_hdr_off + pkt_idx);
        if ((uintptr_t)pkt_hdr >= (uintptr_t)rxhdr) {
            zxlogf(ERROR, "%s packet header out of bounds, packet header=%p rx header=%p\n",
                    __func__, pkt_hdr, rxhdr);
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
        uint16_t pkt_len = le16toh((*pkt_hdr & AX88179_RX_PKTLEN) >> 16);
        zxlogf(SPEW, "pkt_hdr: %0#x pkt_len: %u\n", *pkt_hdr, pkt_len);
        if (pkt_len < 2) {
            zxlogf(ERROR, "%s short packet (len=%u)\n", __func__,  pkt_len);
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
        if (offset + pkt_len > rxhdr->pkt_hdr_off) {
            zxlogf(ERROR, "%s invalid packet length %u > %lu bytes remaining\n",
                    __func__, pkt_len, rxhdr->pkt_hdr_off - offset);
            return ZX_ERR_IO_DATA_INTEGRITY;
        }

        bool drop = false;
        if (*pkt_hdr & AX88179_RX_DROPPKT) {
            zxlogf(SPEW, "%s DropPkt\n", __func__);
            drop = true;
        }
        if (*pkt_hdr & AX88179_RX_MIIER) {
            zxlogf(SPEW, "%s MII-Er\n", __func__);
            drop = true;
        }
        if (*pkt_hdr & AX88179_RX_CRCER) {
            zxlogf(SPEW, "%s CRC-Er\n", __func__);
            drop = true;
        }
        if (!(*pkt_hdr & AX88179_RX_OK)) {
            zxlogf(SPEW, "%s !GoodPkt\n", __func__);
            drop = true;
        }
        if (!drop) {
            zxlogf(SPEW, "offset = %zd\n", offset);
            eth->ifc->recv(eth->cookie, read_data + offset + 2, pkt_len - 2, 0);
        }

        // Advance past this packet in the completed read
        offset += pkt_len;
        offset = ALIGN(offset, 8);
    }

    return ZX_OK;
}

static void ax88179_read_complete(usb_request_t* request, void* cookie) {
    ax88179_t* eth = (ax88179_t*)cookie;

    if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
        usb_req_release(&eth->usb, request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->response.status == ZX_ERR_IO_REFUSED) {
        zxlogf(TRACE, "ax88179_read_complete usb_reset_endpoint\n");
        usb_reset_endpoint(&eth->usb, eth->bulk_in_addr);
    } else if (request->response.status == ZX_ERR_IO_INVALID) {
        zxlogf(TRACE, "ax88179_read_complete Slowing down the requests by %d usec"
               " and resetting the recv endpoint\n", ETHMAC_RECV_DELAY);
        if (eth->rx_endpoint_delay < ETHMAC_MAX_RECV_DELAY) {
            eth->rx_endpoint_delay += ETHMAC_RECV_DELAY;
        }
        usb_reset_endpoint(&eth->usb, eth->bulk_in_addr);
    } else if ((request->response.status == ZX_OK) && eth->ifc) {
        ax88179_recv(eth, request);
    }

    if (eth->online) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(eth->rx_endpoint_delay)));
        usb_request_queue(&eth->usb, request);
    } else {
        list_add_head(&eth->free_read_reqs, &request->node);
    }
    mtx_unlock(&eth->mutex);
}

static zx_status_t ax88179_append_to_tx_req(usb_protocol_t* usb, usb_request_t* req,
                                            ethmac_netbuf_t* netbuf) {
    zx_off_t offset = ALIGN(req->header.length, 4);
    if (offset + sizeof(ax88179_tx_hdr_t) + netbuf->len > USB_BUF_SIZE) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    ax88179_tx_hdr_t hdr = {
        .tx_len = htole16(netbuf->len),
    };
    usb_req_copy_to(usb, req, &hdr, sizeof(hdr), offset);
    usb_req_copy_to(usb, req, netbuf->data, netbuf->len, offset + sizeof(hdr));
    req->header.length = offset + sizeof(hdr) + netbuf->len;
    return ZX_OK;
}

static void ax88179_write_complete(usb_request_t* request, void* cookie) {
    zxlogf(DEBUG1, "ax88179: write complete\n");
    ax88179_t* eth = (ax88179_t*)cookie;

    if (request->response.status == ZX_ERR_IO_NOT_PRESENT) {
        usb_req_release(&eth->usb, request);
        return;
    }

    mtx_lock(&eth->tx_lock);
    ZX_DEBUG_ASSERT(eth->usb_tx_in_flight <= MAX_TX_IN_FLIGHT);

    if (!list_is_empty(&eth->pending_netbuf)) {
        // If we have any pending netbufs, add them to the recently-freed usb request
        request->header.length = 0;
        ethmac_netbuf_t* next_netbuf = list_peek_head_type(&eth->pending_netbuf, ethmac_netbuf_t,
                                                           node);
        while (next_netbuf != NULL && ax88179_append_to_tx_req(&eth->usb, request,
                                                               next_netbuf) == ZX_OK) {
            list_remove_head_type(&eth->pending_netbuf, ethmac_netbuf_t, node);
            mtx_lock(&eth->mutex);
            if (eth->ifc) {
                eth->ifc->complete_tx(eth->cookie, next_netbuf, ZX_OK);
            }
            mtx_unlock(&eth->mutex);
            next_netbuf = list_peek_head_type(&eth->pending_netbuf, ethmac_netbuf_t, node);
        }
        list_add_tail(&eth->pending_usb_tx, &request->node);
    } else {
        list_add_tail(&eth->free_write_reqs, &request->node);
    }

    if (request->response.status == ZX_ERR_IO_REFUSED) {
        zxlogf(TRACE, "ax88179_write_complete usb_reset_endpoint\n");
        usb_reset_endpoint(&eth->usb, eth->bulk_out_addr);
    } else if (request->response.status == ZX_ERR_IO_INVALID) {
        zxlogf(TRACE, "ax88179_write_complete Slowing down the requests by %d usec"
               " and resetting the transmit endpoint\n", ETHMAC_TRANSMIT_DELAY);
        if (eth->tx_endpoint_delay < ETHMAC_MAX_TRANSMIT_DELAY) {
            eth->tx_endpoint_delay += ETHMAC_TRANSMIT_DELAY;
        }
        usb_reset_endpoint(&eth->usb, eth->bulk_out_addr);
    }
    usb_request_t* next = list_remove_head_type(&eth->pending_usb_tx, usb_request_t, node);
    if (next == NULL) {
        eth->usb_tx_in_flight--;
        zxlogf(DEBUG1, "ax88179: no pending write reqs, %u outstanding\n", eth->usb_tx_in_flight);
    } else {
        zxlogf(DEBUG1, "ax88179: queuing request (%p) of length %lu, %u outstanding\n",
                 next, next->header.length, eth->usb_tx_in_flight);
        zx_nanosleep(zx_deadline_after(ZX_USEC(eth->tx_endpoint_delay)));
        usb_request_queue(&eth->usb, next);
    }
    ZX_DEBUG_ASSERT(eth->usb_tx_in_flight <= MAX_TX_IN_FLIGHT);
    mtx_unlock(&eth->tx_lock);
}

static void ax88179_interrupt_complete(usb_request_t* request, void* cookie) {
    ax88179_t* eth = (ax88179_t*)cookie;
    sync_completion_signal(&eth->completion);
}

static void ax88179_handle_interrupt(ax88179_t* eth, usb_request_t* request) {
    mtx_lock(&eth->mutex);
    if (request->response.status == ZX_OK && request->response.actual == sizeof(eth->status)) {
        uint8_t status[INTR_REQ_SIZE];

        usb_req_copy_from(&eth->usb, request, status, sizeof(status), 0);
        if (memcmp(eth->status, status, sizeof(eth->status))) {
            const uint8_t* b = status;
            zxlogf(TRACE, "ax88179 status changed: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
            memcpy(eth->status, status, sizeof(eth->status));
            uint8_t bb = eth->status[2];
            bool online = (bb & 1) != 0;
            bool was_online = eth->online;
            eth->online = online;
            if (online && !was_online) {
                ax88179_configure_medium_mode(eth);
                // Now that we are online, queue all our read requests
                usb_request_t* req;
                usb_request_t* prev;
                list_for_every_entry_safe (&eth->free_read_reqs, req, prev, usb_request_t, node) {
                    list_delete(&req->node);
                    usb_request_queue(&eth->usb, req);
                }
                zxlogf(TRACE, "ax88179 now online\n");
                if (eth->ifc) {
                    eth->ifc->status(eth->cookie, ETH_STATUS_ONLINE);
                }
            } else if (!online && was_online) {
                zxlogf(TRACE, "ax88179 now offline\n");
                if (eth->ifc) {
                    eth->ifc->status(eth->cookie, 0);
                }
            }
        }
    }

    mtx_unlock(&eth->mutex);
}

static zx_status_t ax88179_queue_tx(void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) {
    size_t length = netbuf->len;

    if (length > (AX88179_MTU + MAX_ETH_HDRS)) {
        zxlogf(ERROR, "ax88179: unsupported packet length %zu\n", length);
        return ZX_ERR_INVALID_ARGS;
    }

    ax88179_t* eth = ctx;

    mtx_lock(&eth->tx_lock);
    ZX_DEBUG_ASSERT(eth->usb_tx_in_flight <= MAX_TX_IN_FLIGHT);

    zx_nanosleep(zx_deadline_after(ZX_USEC(eth->tx_endpoint_delay)));
    // If we already have entries in our pending_netbuf list we should put this one there, too.
    // Otherwise, we may end up reordering packets.
    if (!list_is_empty(&eth->pending_netbuf)) {
        goto bufs_full;
    }

    // Find the last entry in the pending_usb_tx list
    usb_request_t* req = NULL;
    if (list_is_empty(&eth->pending_usb_tx)) {
        zxlogf(DEBUG1, "ax88179: no pending reqs, getting free write req\n");
        req = list_remove_head_type(&eth->free_write_reqs, usb_request_t, node);
        if (req == NULL) {
            goto bufs_full;
        }
        req->header.length = 0;
        list_add_tail(&eth->pending_usb_tx, &req->node);
    } else {
        req = list_peek_tail_type(&eth->pending_usb_tx, usb_request_t, node);
        zxlogf(DEBUG1, "ax88179: got tail req (%p)\n", req);
    }

    zxlogf(DEBUG1, "ax88179: current req len=%lu, next packet len=%zu\n",
            req->header.length, length);

    if (ax88179_append_to_tx_req(&eth->usb, req, netbuf) == ZX_ERR_BUFFER_TOO_SMALL) {
        // Our data won't fit - grab a new request
        zxlogf(DEBUG1, "ax88179: getting new write req\n");
        req = list_remove_head_type(&eth->free_write_reqs, usb_request_t, node);
        if (req == NULL) {
            goto bufs_full;
        }
        req->header.length = 0;
        list_add_tail(&eth->pending_usb_tx, &req->node);
        ax88179_append_to_tx_req(&eth->usb, req, netbuf);
    } else if (options & ETHMAC_TX_OPT_MORE) {
        // Don't send data if we have more coming that might fit into the current request. If we
        // already filled up a request, though, we should write it out if we can.
        zxlogf(DEBUG1, "ax88179: waiting for more data, %u outstanding\n", eth->usb_tx_in_flight);
        mtx_unlock(&eth->tx_lock);
        return ZX_OK;
    }

    if (eth->usb_tx_in_flight == MAX_TX_IN_FLIGHT) {
        zxlogf(DEBUG1, "ax88179: max outstanding tx, waiting\n");
        mtx_unlock(&eth->tx_lock);
        return ZX_OK;
    }
    req = list_remove_head_type(&eth->pending_usb_tx, usb_request_t, node);
    zxlogf(DEBUG1, "ax88179: queuing request (%p) of length %lu, %u outstanding\n",
             req, req->header.length, eth->usb_tx_in_flight);
    usb_request_queue(&eth->usb, req);
    eth->usb_tx_in_flight++;
    ZX_DEBUG_ASSERT(eth->usb_tx_in_flight <= MAX_TX_IN_FLIGHT);
    mtx_unlock(&eth->tx_lock);
    return ZX_OK;

bufs_full:
    list_add_tail(&eth->pending_netbuf, &netbuf->node);
    zxlogf(DEBUG1, "ax88179: buffers full, there are %zu pending netbufs\n",
            list_length(&eth->pending_netbuf));
    mtx_unlock(&eth->tx_lock);
    return ZX_ERR_SHOULD_WAIT;
}

static void ax88179_unbind(void* ctx) {
    ax88179_t* eth = ctx;
    device_remove(eth->device);
}

static void ax88179_free(ax88179_t* eth) {
    usb_request_t* req;
    while ((req = list_remove_head_type(&eth->free_read_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&eth->usb, req);
    }
    while ((req = list_remove_head_type(&eth->free_write_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&eth->usb, req);
    }
    while ((req = list_remove_head_type(&eth->pending_usb_tx, usb_request_t, node)) != NULL) {
        usb_req_release(&eth->usb, req);
    }
    usb_req_release(&eth->usb, eth->interrupt_req);

    free(eth);
}

static void ax88179_release(void* ctx) {
    ax88179_t* eth = ctx;

    // wait for thread to finish before cleaning up
    thrd_join(eth->thread, NULL);

    ax88179_free(eth);
}

static zx_protocol_device_t ax88179_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = ax88179_unbind,
    .release = ax88179_release,
};


static zx_status_t ax88179_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    ax88179_t* eth = ctx;

    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    info->mtu = 1500;
    memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));

    return ZX_OK;
}

static void ax88179_stop(void* ctx) {
    ax88179_t* eth = ctx;
    mtx_lock(&eth->mutex);
    eth->ifc = NULL;
    mtx_unlock(&eth->mutex);
}

static zx_status_t ax88179_start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
    ax88179_t* eth = ctx;
    zx_status_t status = ZX_OK;

    mtx_lock(&eth->mutex);
    if (eth->ifc) {
        status = ZX_ERR_BAD_STATE;
    } else {
        eth->ifc = ifc;
        eth->cookie = cookie;
        eth->ifc->status(eth->cookie, eth->online ? ETH_STATUS_ONLINE : 0);
    }
    mtx_unlock(&eth->mutex);

    return status;
}

static zx_status_t ax88179_twiddle_rcr_bit(ax88179_t* eth, uint16_t bit, bool on) {
    uint16_t rcr_bits;
    zx_status_t status = ax88179_read_mac(eth, AX88179_MAC_RCR, 2, &rcr_bits);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ax88179_read_mac from %#x failed: %d\n", AX88179_MAC_RCR, status);
        return status;
    }
    if (on) {
        rcr_bits |= bit;
    } else {
        rcr_bits &= ~bit;
    }
    status = ax88179_write_mac(eth, AX88179_MAC_RCR, 2, &rcr_bits);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RCR, status);
    }
    return status;
}

static inline zx_status_t ax88179_set_promisc(ax88179_t* eth, bool on) {
    return ax88179_twiddle_rcr_bit(eth, AX88179_RCR_PROMISC, on);
}

static inline zx_status_t ax88179_set_multicast_promisc(ax88179_t* eth, bool on) {
    if (eth->multicast_filter_overflow && !on) {
        return ZX_OK;
    }
    return ax88179_twiddle_rcr_bit(eth, AX88179_RCR_AMALL, on);
}

static void set_filter_bit(const uint8_t* mac, uint8_t* filter) {
    // Invert the seed (standard is ~0) and output to get usable bits.
    uint32_t crc = ~crc32(0, mac, ETH_MAC_SIZE);
    uint8_t reverse[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    filter[reverse[crc & 7]] |= 1 << reverse[(crc >> 3) & 7];
}

static zx_status_t ax88179_set_multicast_filter(ax88179_t* eth, int32_t n_addresses,
                                                uint8_t* address_bytes) {
    zx_status_t status = ZX_OK;
    eth->multicast_filter_overflow = (n_addresses == ETHMAC_MULTICAST_FILTER_OVERFLOW) ||
        (n_addresses > MAX_MULTICAST_FILTER_ADDRS);
    if (eth->multicast_filter_overflow) {
        status = ax88179_set_multicast_promisc(eth, true);
        return status;
    }

    uint8_t filter[MULTICAST_FILTER_NBYTES];
    memset(filter, 0, MULTICAST_FILTER_NBYTES);
    for (int32_t i = 0; i < n_addresses; i++) {
        set_filter_bit(address_bytes + i * ETH_MAC_SIZE, filter);
    }
    status = ax88179_write_mac(eth, AX88179_MAC_MFA, MULTICAST_FILTER_NBYTES, &filter);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_MFA, status);
        return status;
    }
    return status;
}

static void ax88179_dump_regs(ax88179_t* eth);
static zx_status_t ax88179_set_param(void *ctx, uint32_t param, int32_t value, void* data) {
    ax88179_t* eth = ctx;
    zx_status_t status = ZX_OK;

    mtx_lock(&eth->mutex);

    switch (param) {
    case ETHMAC_SETPARAM_PROMISC:
        status = ax88179_set_promisc(eth, (bool)value);
        break;
    case ETHMAC_SETPARAM_MULTICAST_PROMISC:
        status = ax88179_set_multicast_promisc(eth, (bool)value);
        break;
    case ETHMAC_SETPARAM_MULTICAST_FILTER:
        status = ax88179_set_multicast_filter(eth, value, (uint8_t*)data);
        break;
    case ETHMAC_SETPARAM_DUMP_REGS:
        ax88179_dump_regs(eth);
        break;
    default:
        status = ZX_ERR_NOT_SUPPORTED;
    }
    mtx_unlock(&eth->mutex);

    return status;
}

static ethmac_protocol_ops_t ethmac_ops = {
    .query = ax88179_query,
    .stop = ax88179_stop,
    .start = ax88179_start,
    .queue_tx = ax88179_queue_tx,
    .set_param = ax88179_set_param,
};


#define READ_REG(r, len) \
    do { \
        reg = 0; \
        zx_status_t status = ax88179_read_mac(eth, r, len, &reg); \
        if (status < 0) { \
            zxlogf(ERROR, "ax88179: could not read reg " #r ": %d\n", status); \
        } else { \
            zxlogf(SPEW, "ax88179: reg " #r " = %" PRIx64 "\n", reg); \
        } \
    } while(0)

static void ax88179_dump_regs(ax88179_t* eth) {
    uint64_t reg = 0;
    READ_REG(AX88179_MAC_PLSR, 1);
    READ_REG(AX88179_MAC_GSR, 1);
    READ_REG(AX88179_MAC_SMSR, 1);
    READ_REG(AX88179_MAC_CSR, 1);
    READ_REG(AX88179_MAC_RCR, 2);
    READ_REG(AX88179_MAC_MFA, MULTICAST_FILTER_NBYTES);
    READ_REG(AX88179_MAC_IPGCR, 3);
    READ_REG(AX88179_MAC_TR, 1);
    READ_REG(AX88179_MAC_MSR, 2);
    READ_REG(AX88179_MAC_MMSR, 1);
}

static int ax88179_thread(void* arg) {
    ax88179_t* eth = (ax88179_t*)arg;

    uint32_t data = 0;
    // Enable embedded PHY
    zx_status_t status = ax88179_write_mac(eth, AX88179_MAC_EPPRCR, 2, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_EPPRCR, status);
        goto fail;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    data = 0x0020;
    status = ax88179_write_mac(eth, AX88179_MAC_EPPRCR, 2, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_EPPRCR, status);
        goto fail;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));

    // Switch clock to normal speed
    data = 0x03;
    status = ax88179_write_mac(eth, AX88179_MAC_CLKSR, 1, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CLKSR, status);
        goto fail;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

    // Read the MAC addr
    status = ax88179_read_mac(eth, AX88179_MAC_NIDR, 6, eth->mac_addr);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_read_mac to %#x failed: %d\n", AX88179_MAC_NIDR, status);
        goto fail;
    }

    zxlogf(INFO, "ax88179 MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
            eth->mac_addr[0], eth->mac_addr[1], eth->mac_addr[2],
            eth->mac_addr[3], eth->mac_addr[4], eth->mac_addr[5]);

    ///* Disabled for now
    // Ensure that the MAC RX is disabled
    data = 0;
    status = ax88179_write_mac(eth, AX88179_MAC_RCR, 2, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RCR, status);
        goto fail;
    }
    //*/

    // Set RX Bulk-in sizes -- use USB 3.0/1000Mbps at this point
    status = ax88179_configure_bulk_in(eth, AX88179_PLSR_USB_SS|AX88179_PLSR_EPHY_1000);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RQCR, status);
        goto fail;
    }

    // Configure flow control watermark
    data = 0x3c;
    status = ax88179_write_mac(eth, AX88179_MAC_PWLLR, 1, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_PWLLR, status);
        goto fail;
    }
    data = 0x5c;
    status = ax88179_write_mac(eth, AX88179_MAC_PWLHR, 1, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_PWLHR, status);
        goto fail;
    }

    // RX/TX checksum offload: ipv4, tcp, udp, tcpv6, udpv6
    data = (1<<6) | (1<<5) | (1<<2) | (1<<1) | (1<<0);
    status = ax88179_write_mac(eth, AX88179_MAC_CRCR, 1, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CRCR, status);
        goto fail;
    }
    status = ax88179_write_mac(eth, AX88179_MAC_CTCR, 1, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CTCR, status);
        goto fail;
    }

    // TODO: PHY LED

    // PHY auto-negotiation
    uint16_t phy_data = 0;
    status = ax88179_read_phy(eth, AX88179_PHY_BMCR, &phy_data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_read_phy to %#x failed: %d\n", AX88179_PHY_BMCR, status);
        goto fail;
    }
    phy_data |= 0x1200;
    status = ax88179_write_phy(eth, AX88179_PHY_BMCR, phy_data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_phy to %#x failed: %d\n", AX88179_PHY_BMCR, status);
        goto fail;
    }

    // Default Ethernet medium mode
    data = 0x013b;
    status = ax88179_write_mac(eth, AX88179_MAC_MSR, 2, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_MSR, status);
        goto fail;
    }

    // Enable MAC RX
    // TODO(eventually): Once we get IGMP, turn off AMALL unless someone wants it.
    data = AX88179_RCR_AMALL | AX88179_RCR_AB | AX88179_RCR_AM | AX88179_RCR_SO |
        AX88179_RCR_DROP_CRCE_N | AX88179_RCR_IPE_N;
    status = ax88179_write_mac(eth, AX88179_MAC_RCR, 2, &data);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RCR, status);
        goto fail;
    }

    uint8_t filter[MULTICAST_FILTER_NBYTES];
    memset(filter, 0, MULTICAST_FILTER_NBYTES);
    status = ax88179_write_mac(eth, AX88179_MAC_MFA, MULTICAST_FILTER_NBYTES, &filter);
    if (status < 0) {
        zxlogf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_MFA, status);
        goto fail;
    }

    // Make the device visible
    device_make_visible(eth->device);

    uint64_t count = 0;
    usb_request_t* req = eth->interrupt_req;
    while (true) {
        sync_completion_reset(&eth->completion);
        usb_request_queue(&eth->usb, req);
        sync_completion_wait(&eth->completion, ZX_TIME_INFINITE);
        if (req->response.status != ZX_OK) {
            return req->response.status;
        }
        count++;
        ax88179_handle_interrupt(eth, req);
#if AX88179_DEBUG_VERBOSE
        if (count % 32 == 0) {
            ax88179_dump_regs(eth);
        }
#endif
    }

fail:
    device_remove(eth->device);
    return status;
}

static zx_status_t ax88179_bind(void* ctx, zx_device_t* device) {
    zxlogf(TRACE, "ax88179_bind\n");

    usb_protocol_t usb;
    zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
    if (result != ZX_OK) {
        return result;
    }

    // find our endpoints
    usb_desc_iter_t iter;
    result = usb_desc_iter_init(&usb, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf || intf->bNumEndpoints != 3) {
        usb_desc_iter_release(&iter);
        return ZX_ERR_NOT_SUPPORTED;
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
        zxlogf(ERROR, "ax88179_bind could not find endpoints\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    ax88179_t* eth = calloc(1, sizeof(ax88179_t));
    if (!eth) {
        zxlogf(ERROR, "Not enough memory for ax88179_t\n");
        return ZX_ERR_NO_MEMORY;
    }

    list_initialize(&eth->free_read_reqs);
    list_initialize(&eth->free_write_reqs);
    list_initialize(&eth->pending_usb_tx);
    list_initialize(&eth->pending_netbuf);
    mtx_init(&eth->tx_lock, mtx_plain);
    mtx_init(&eth->mutex, mtx_plain);

    eth->usb_device = device;
    memcpy(&eth->usb, &usb, sizeof(eth->usb));
    eth->bulk_in_addr = bulk_in_addr;
    eth->bulk_out_addr = bulk_out_addr;

    eth->rx_endpoint_delay = ETHMAC_INITIAL_RECV_DELAY;
    eth->tx_endpoint_delay = ETHMAC_INITIAL_TRANSMIT_DELAY;
    zx_status_t status = ZX_OK;
    for (int i = 0; i < READ_REQ_COUNT; i++) {
        usb_request_t* req;
        status = usb_req_alloc(&eth->usb, &req, USB_BUF_SIZE, bulk_in_addr);
        if (status != ZX_OK) {
            goto fail;
        }
        req->complete_cb = ax88179_read_complete;
        req->cookie = eth;
        list_add_head(&eth->free_read_reqs, &req->node);
    }
    for (int i = 0; i < WRITE_REQ_COUNT; i++) {
        usb_request_t* req;
        status = usb_req_alloc(&eth->usb, &req, USB_BUF_SIZE, bulk_out_addr);
        if (status != ZX_OK) {
            goto fail;
        }
        req->complete_cb = ax88179_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }
    usb_request_t* int_req;
    status = usb_req_alloc(&eth->usb, &int_req, INTR_REQ_SIZE, intr_addr);
    if (status != ZX_OK) {
        goto fail;
    }
    int_req->complete_cb = ax88179_interrupt_complete;
    int_req->cookie = eth;
    eth->interrupt_req = int_req;

    /* This is not needed, as long as the xhci stack does it for us.
    status = usb_set_configuration(device, 1);
    if (status < 0) {
        zxlogf(ERROR, "aax88179_bind could not set configuration: %d\n", status);
        return ZX_ERR_NOT_SUPPORTED;
    }
    */

    // Create the device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ax88179",
        .ctx = eth,
        .ops = &ax88179_device_proto,
        .flags = DEVICE_ADD_INVISIBLE,
        .proto_id = ZX_PROTOCOL_ETHERNET_IMPL,
        .proto_ops = &ethmac_ops,
    };

    status = device_add(eth->usb_device, &args, &eth->device);
    if (status < 0) {
        zxlogf(ERROR, "ax88179: failed to create device: %d\n", status);
        goto fail;
    }


    int ret = thrd_create_with_name(&eth->thread, ax88179_thread, eth, "ax88179_thread");
    if (ret != thrd_success) {
        device_remove(eth->device);
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;

fail:
    zxlogf(ERROR, "ax88179_bind failed: %d\n", status);
    ax88179_free(eth);
    return status;
}

static zx_driver_ops_t ax88179_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ax88179_bind,
};

ZIRCON_DRIVER_BEGIN(ethernet_ax88179, ax88179_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, ASIX_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, AX88179_PID),
ZIRCON_DRIVER_END(ethernet_ax88179)
