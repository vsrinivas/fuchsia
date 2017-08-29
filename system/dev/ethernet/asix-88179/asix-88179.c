// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <driver/usb.h>
#include <magenta/assert.h>
#include <magenta/device/ethernet.h>
#include <magenta/listnode.h>
#include <pretty/hexdump.h>
#include <sync/completion.h>

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

typedef struct {
    mx_device_t* device;
    mx_device_t* usb_device;
    usb_protocol_t usb;

    uint8_t mac_addr[6];
    uint8_t status[INTR_REQ_SIZE];
    bool online;
    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;

    // interrupt in request
    iotxn_t* interrupt_req;
    completion_t completion;

    // pool of free USB bulk requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;

    // Locks the tx_in_flight and pending_tx list.
    mtx_t tx_lock;
    // Whether an iotxn has been queued to the USB device.
    uint8_t tx_in_flight;
    // List of iotxns that have pending data. Used to buffer data if a USB transaction is in flight.
    // Additional data must be appended to the tail of the list, or if that's full, an iotxn from
    // free_write_reqs must be added to the list.
    list_node_t pending_tx;

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

static mx_status_t ax88179_read_mac(ax88179_t* eth, uint8_t reg_addr, uint8_t reg_len, void* data) {
    mx_status_t status = usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                     AX88179_REQ_MAC, reg_addr, reg_len, data, reg_len,
                                     MX_TIME_INFINITE);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        dprintf(SPEW, "read mac %#x:\n", reg_addr);
        if (status > 0) {
            hexdump8(data, status);
        }
    }
    return status;
}

static mx_status_t ax88179_write_mac(ax88179_t* eth, uint8_t reg_addr, uint8_t reg_len, void* data) {
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        dprintf(SPEW, "write mac %#x:\n", reg_addr);
        hexdump8(data, reg_len);
    }
    return usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_REQ_MAC,
                       reg_addr, reg_len, data, reg_len, MX_TIME_INFINITE);
}

static mx_status_t ax88179_read_phy(ax88179_t* eth, uint8_t reg_addr, uint16_t* data) {
    mx_status_t status = usb_control(&eth->usb, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                     AX88179_REQ_PHY, AX88179_PHY_ID, reg_addr, data, sizeof(*data),
                                     MX_TIME_INFINITE);
    if (status == sizeof(*data)) {
        dprintf(SPEW, "read phy %#x: %#x\n", reg_addr, *data);
    }
    return status;
}

static mx_status_t ax88179_write_phy(ax88179_t* eth, uint8_t reg_addr, uint16_t data) {
    dprintf(SPEW, "write phy %#x: %#x\n", reg_addr, data);
    return usb_control(&eth->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_REQ_PHY,
                       AX88179_PHY_ID, reg_addr, &data, sizeof(data), MX_TIME_INFINITE);
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

static mx_status_t ax88179_configure_bulk_in(ax88179_t* eth, uint8_t plsr) {
    uint8_t usb_mode = plsr & AX88179_PLSR_USB_MASK;
    if (usb_mode & (usb_mode-1)) {
        dprintf(ERROR, "ax88179: invalid usb mode: %#x\n", usb_mode);
        return MX_ERR_INVALID_ARGS;
    }

    uint8_t speed = plsr & AX88179_PLSR_EPHY_MASK;
    if (speed & (speed-1)) {
        dprintf(ERROR, "ax88179: invalid eth speed: %#x\n", speed);
    }

    mx_status_t status = ax88179_write_mac(eth, AX88179_MAC_RQCR, 5,
            ax88179_bulk_in_config[usb_mode][speed >> 4]);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RQCR, status);
    }
    return status;
}

static mx_status_t ax88179_configure_medium_mode(ax88179_t* eth) {
    uint16_t data = 0;
    mx_status_t status = ax88179_read_phy(eth, AX88179_PHY_PHYSR, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_read_phy to %#x failed: %d\n", AX88179_PHY_PHYSR, status);
        return status;
    }

    unsigned int mode = (data & (AX88179_PHYSR_SPEED|AX88179_PHYSR_DUPLEX)) >> 13;
    dprintf(TRACE, "ax88179 medium mode: %#x\n", mode);
    if (mode == 4 || mode > 5) {
        dprintf(ERROR, "ax88179 mode invalid (mode=%u)\n", mode);
        return MX_ERR_NOT_SUPPORTED;
    }
    status = ax88179_write_mac(eth, AX88179_MAC_MSR, 2, ax88179_media_mode[mode]);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_MSR, status);
        return status;
    }

    data = 0;
    status = ax88179_read_mac(eth, AX88179_MAC_PLSR, 1, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_read_mac to %#x failed: %d\n", AX88179_MAC_PLSR, status);
        return status;
    }
    status = ax88179_configure_bulk_in(eth, data & 0xff);

    return status;
}

static mx_status_t ax88179_recv(ax88179_t* eth, iotxn_t* request) {
    dprintf(SPEW, "request len %" PRIu64"\n", request->actual);

    if (request->actual < 4) {
        dprintf(ERROR, "ax88179_recv short packet\n");
        return MX_ERR_INTERNAL;
    }

    uint8_t* read_data = NULL;
    iotxn_mmap(request, (void*)&read_data);

    ptrdiff_t rxhdr_off = request->actual - sizeof(ax88179_rx_hdr_t);
    ax88179_rx_hdr_t* rxhdr = (ax88179_rx_hdr_t*)(read_data + rxhdr_off);
    dprintf(SPEW, "rxhdr offset %u, num %u\n", rxhdr->pkt_hdr_off, rxhdr->num_pkts);
    if (rxhdr->num_pkts < 1 || rxhdr->pkt_hdr_off >= rxhdr_off) {
        dprintf(ERROR, "%s bad packet\n", __func__);
        return MX_ERR_IO_DATA_INTEGRITY;
    }

    size_t offset = 0;
    size_t packet = 0;

    while (packet < rxhdr->num_pkts) {
        dprintf(SPEW, "next packet: %zd\n", packet);
        ptrdiff_t pkt_idx = packet++ * sizeof(uint32_t);
        uint32_t* pkt_hdr = (uint32_t*)(read_data + rxhdr->pkt_hdr_off + pkt_idx);
        if ((uintptr_t)pkt_hdr >= (uintptr_t)rxhdr) {
            dprintf(ERROR, "%s packet header out of bounds, packet header=%p rx header=%p\n",
                    __func__, pkt_hdr, rxhdr);
            return MX_ERR_IO_DATA_INTEGRITY;
        }
        uint16_t pkt_len = le16toh((*pkt_hdr & AX88179_RX_PKTLEN) >> 16);
        dprintf(SPEW, "pkt_hdr: %0#x pkt_len: %u\n", *pkt_hdr, pkt_len);
        if (pkt_len < 2) {
            dprintf(ERROR, "%s short packet (len=%u)\n", __func__,  pkt_len);
            return MX_ERR_IO_DATA_INTEGRITY;
        }
        if (offset + pkt_len > rxhdr->pkt_hdr_off) {
            dprintf(ERROR, "%s invalid packet length %u > %lu bytes remaining\n",
                    __func__, pkt_len, rxhdr->pkt_hdr_off - offset);
            return MX_ERR_IO_DATA_INTEGRITY;
        }

        bool drop = false;
        if (*pkt_hdr & AX88179_RX_DROPPKT) {
            dprintf(SPEW, "%s DropPkt\n", __func__);
            drop = true;
        }
        if (*pkt_hdr & AX88179_RX_MIIER) {
            dprintf(SPEW, "%s MII-Er\n", __func__);
            drop = true;
        }
        if (*pkt_hdr & AX88179_RX_CRCER) {
            dprintf(SPEW, "%s CRC-Er\n", __func__);
            drop = true;
        }
        if (!(*pkt_hdr & AX88179_RX_OK)) {
            dprintf(SPEW, "%s !GoodPkt\n", __func__);
            drop = true;
        }
        if (!drop) {
            dprintf(SPEW, "offset = %zd\n", offset);
            eth->ifc->recv(eth->cookie, read_data + offset + 2, pkt_len - 2, 0);
        }

        // Advance past this packet in the completed read
        offset += pkt_len;
        offset = ALIGN(offset, 8);
    }

    return MX_OK;
}

static void ax88179_read_complete(iotxn_t* request, void* cookie) {
    ax88179_t* eth = (ax88179_t*)cookie;

    if (request->status == MX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    if (request->status == MX_ERR_IO_REFUSED) {
        dprintf(TRACE, "ax88179_read_complete usb_reset_endpoint\n");
        usb_reset_endpoint(&eth->usb, eth->bulk_in_addr);
    } else if ((request->status == MX_OK) && eth->ifc) {
        ax88179_recv(eth, request);
    }

    if (eth->online) {
        iotxn_queue(eth->usb_device, request);
    } else {
        list_add_head(&eth->free_read_reqs, &request->node);
    }
    mtx_unlock(&eth->mutex);
}

static void ax88179_write_complete(iotxn_t* request, void* cookie) {
    dprintf(DEBUG1, "ax88179: write complete\n");
    ax88179_t* eth = (ax88179_t*)cookie;

    if (request->status == MX_ERR_IO_NOT_PRESENT) {
        iotxn_release(request);
        return;
    }

    mtx_lock(&eth->tx_lock);
    MX_DEBUG_ASSERT(eth->tx_in_flight <= MAX_TX_IN_FLIGHT);
    list_add_tail(&eth->free_write_reqs, &request->node);
    if (request->status == MX_ERR_IO_REFUSED) {
        dprintf(TRACE, "ax88179_write_complete usb_reset_endpoint\n");
        usb_reset_endpoint(&eth->usb, eth->bulk_out_addr);
    }

    iotxn_t* next = list_remove_head_type(&eth->pending_tx, iotxn_t, node);
    if (next == NULL) {
        eth->tx_in_flight--;
        dprintf(DEBUG1, "ax88179: no pending write txns, %u outstanding\n", eth->tx_in_flight);
    } else {
        dprintf(DEBUG1, "ax88179: queuing iotxn (%p) of length %lu, %u outstanding\n",
                 next, next->length, eth->tx_in_flight);
        iotxn_queue(eth->usb_device, next);
    }
    MX_DEBUG_ASSERT(eth->tx_in_flight <= MAX_TX_IN_FLIGHT);
    mtx_unlock(&eth->tx_lock);
}

static void ax88179_interrupt_complete(iotxn_t* request, void* cookie) {
    ax88179_t* eth = (ax88179_t*)cookie;
    completion_signal(&eth->completion);
}

static void ax88179_handle_interrupt(ax88179_t* eth, iotxn_t* request) {
    mtx_lock(&eth->mutex);
    if (request->status == MX_OK && request->actual == sizeof(eth->status)) {
        uint8_t status[INTR_REQ_SIZE];

        iotxn_copyfrom(request, status, sizeof(status), 0);
        if (memcmp(eth->status, status, sizeof(eth->status))) {
            const uint8_t* b = status;
            dprintf(TRACE, "ax88179 status changed: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
            memcpy(eth->status, status, sizeof(eth->status));
            uint8_t bb = eth->status[2];
            bool online = (bb & 1) != 0;
            bool was_online = eth->online;
            eth->online = online;
            if (online && !was_online) {
                ax88179_configure_medium_mode(eth);
                // Now that we are online, queue all our read requests
                iotxn_t* req;
                iotxn_t* prev;
                list_for_every_entry_safe (&eth->free_read_reqs, req, prev, iotxn_t, node) {
                    list_delete(&req->node);
                    iotxn_queue(eth->usb_device, req);
                }
                dprintf(TRACE, "ax88179 now online\n");
                if (eth->ifc) {
                    eth->ifc->status(eth->cookie, ETH_STATUS_ONLINE);
                }
            } else if (!online && was_online) {
                dprintf(TRACE, "ax88179 now offline\n");
                if (eth->ifc) {
                    eth->ifc->status(eth->cookie, 0);
                }
            }
        }
    }

    mtx_unlock(&eth->mutex);
}

static void ax88179_send(void* ctx, uint32_t options, void* data, size_t length) {
    if (length > (AX88179_MTU + MAX_ETH_HDRS)) {
        return;
    }

    ax88179_t* eth = ctx;

    mtx_lock(&eth->tx_lock);
    MX_DEBUG_ASSERT(eth->tx_in_flight <= MAX_TX_IN_FLIGHT);
    // 1. Find the iotxn we will be writing into.
    //   a) If pending_tx is empty, grab an iotxn from free_write_reqs.
    //   b) Else take the tail of pending_tx.
    // 2. If the alignment + sizeof(hdr) + length > USB_BUF_SIZE, grab an iotxn from free_write_reqs
    //    and add it to the tail of pending_tx.
    // 3. Write to the next 32-byte aligned offset in the iotxn.
    // 4. If ETHMAC_TX_OPT_MORE, return.
    // 5. If tx_in_flight, return.
    // 6. Otherwise, queue the head of pending_tx and set tx_in_flight.

    iotxn_t* txn = NULL;
    if (list_is_empty(&eth->pending_tx)) {
        dprintf(DEBUG1, "ax88179: no pending txns, getting free write req\n");
        txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node);
        if (txn == NULL) {
            dprintf(DEBUG1, "ax88179: no free write txns!\n");
            mtx_unlock(&eth->tx_lock);
            return;
        }
        txn->length = 0;
        list_add_tail(&eth->pending_tx, &txn->node);
    } else {
        txn = list_peek_tail_type(&eth->pending_tx, iotxn_t, node);
        dprintf(DEBUG1, "ax88179: got tail iotxn (%p)\n", txn);
    }

    mx_off_t txn_len = ALIGN(txn->length, 4);
    dprintf(DEBUG1, "ax88179: current iotxn len=%lu, next packet len=%zu\n", txn_len, length);
    if (length > USB_BUF_SIZE - sizeof(ax88179_tx_hdr_t) - txn_len) {
        dprintf(DEBUG1, "ax88179: getting new write req\n");
        txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node);
        if (txn == NULL) {
            dprintf(DEBUG1, "ax88179: no free write txns!\n");
            mtx_unlock(&eth->tx_lock);
            return;
        }
        txn->length = txn_len = 0;
        list_add_tail(&eth->pending_tx, &txn->node);
    }
    dprintf(DEBUG1, "ax88179: txn=%p\n", txn);

    ax88179_tx_hdr_t hdr = {
        .tx_len = htole16(length),
    };

    iotxn_copyto(txn, &hdr, sizeof(hdr), txn_len);
    iotxn_copyto(txn, data, length, txn_len + sizeof(hdr));
    txn->length = txn_len + sizeof(hdr) + length;

    if (options & ETHMAC_TX_OPT_MORE) {
        dprintf(DEBUG1, "ax88179: waiting for more data, %u outstanding\n", eth->tx_in_flight);
        mtx_unlock(&eth->tx_lock);
        return;
    }
    if (eth->tx_in_flight == MAX_TX_IN_FLIGHT) {
        dprintf(DEBUG1, "ax88179: max outstanding tx, waiting\n");
        mtx_unlock(&eth->tx_lock);
        return;
    }
    txn = list_remove_head_type(&eth->pending_tx, iotxn_t, node);
    dprintf(DEBUG1, "ax88179: queuing iotxn (%p) of length %lu, %u outstanding\n",
             txn, txn->length, eth->tx_in_flight);
    iotxn_queue(eth->usb_device, txn);
    eth->tx_in_flight++;
    MX_DEBUG_ASSERT(eth->tx_in_flight <= MAX_TX_IN_FLIGHT);
    mtx_unlock(&eth->tx_lock);
}

static void ax88179_unbind(void* ctx) {
    ax88179_t* eth = ctx;
    device_remove(eth->device);
}

static void ax88179_free(ax88179_t* eth) {
    iotxn_t* txn;
    while ((txn = list_remove_head_type(&eth->free_read_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&eth->pending_tx, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    iotxn_release(eth->interrupt_req);

    free(eth);
}

static void ax88179_release(void* ctx) {
    ax88179_t* eth = ctx;

    // wait for thread to finish before cleaning up
    thrd_join(eth->thread, NULL);

    ax88179_free(eth);
}

static mx_protocol_device_t ax88179_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = ax88179_unbind,
    .release = ax88179_release,
};


static mx_status_t ax88179_query(void* ctx, uint32_t options, ethmac_info_t* info) {
    ax88179_t* eth = ctx;

    if (options) {
        return MX_ERR_INVALID_ARGS;
    }

    memset(info, 0, sizeof(*info));
    info->mtu = 1500;
    memcpy(info->mac, eth->mac_addr, sizeof(eth->mac_addr));

    return MX_OK;
}

static void ax88179_stop(void* ctx) {
    ax88179_t* eth = ctx;
    mtx_lock(&eth->mutex);
    eth->ifc = NULL;
    mtx_unlock(&eth->mutex);
}

static mx_status_t ax88179_start(void* ctx, ethmac_ifc_t* ifc, void* cookie) {
    ax88179_t* eth = ctx;
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

static ethmac_protocol_ops_t ethmac_ops = {
    .query = ax88179_query,
    .stop = ax88179_stop,
    .start = ax88179_start,
    .send = ax88179_send,
};


#define READ_REG(r, len) \
    do { \
        reg = 0; \
        mx_status_t status = ax88179_read_mac(eth, r, len, &reg); \
        if (status < 0) { \
            dprintf(ERROR, "ax88179: could not read reg " #r ": %d\n", status); \
        } else { \
            dprintf(SPEW, "ax88179: reg " #r " = %" PRIx64 "\n", reg); \
        } \
    } while(0)

static void ax88179_dump_regs(ax88179_t* eth) {
    uint64_t reg = 0;
    READ_REG(AX88179_MAC_PLSR, 1);
    READ_REG(AX88179_MAC_GSR, 1);
    READ_REG(AX88179_MAC_SMSR, 1);
    READ_REG(AX88179_MAC_CSR, 1);
    READ_REG(AX88179_MAC_RCR, 2);
    READ_REG(AX88179_MAC_IPGCR, 3);
    READ_REG(AX88179_MAC_TR, 1);
    READ_REG(AX88179_MAC_MSR, 2);
    READ_REG(AX88179_MAC_MMSR, 1);
}

static int ax88179_thread(void* arg) {
    ax88179_t* eth = (ax88179_t*)arg;

    uint32_t data = 0;
    // Enable embedded PHY
    mx_status_t status = ax88179_write_mac(eth, AX88179_MAC_EPPRCR, 2, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_EPPRCR, status);
        goto fail;
    }
    mx_nanosleep(mx_deadline_after(MX_MSEC(1)));
    data = 0x0020;
    status = ax88179_write_mac(eth, AX88179_MAC_EPPRCR, 2, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_EPPRCR, status);
        goto fail;
    }
    mx_nanosleep(mx_deadline_after(MX_MSEC(200)));

    // Switch clock to normal speed
    data = 0x03;
    status = ax88179_write_mac(eth, AX88179_MAC_CLKSR, 1, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CLKSR, status);
        goto fail;
    }
    mx_nanosleep(mx_deadline_after(MX_MSEC(1)));

    // Read the MAC addr
    status = ax88179_read_mac(eth, AX88179_MAC_NIDR, 6, eth->mac_addr);
    if (status < 0) {
        dprintf(ERROR, "ax88179_read_mac to %#x failed: %d\n", AX88179_MAC_NIDR, status);
        goto fail;
    }

    dprintf(INFO, "ax88179 MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
            eth->mac_addr[0], eth->mac_addr[1], eth->mac_addr[2],
            eth->mac_addr[3], eth->mac_addr[4], eth->mac_addr[5]);

    ///* Disabled for now
    // Ensure that the MAC RX is disabled
    data = 0;
    status = ax88179_write_mac(eth, AX88179_MAC_RCR, 2, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RCR, status);
        goto fail;
    }
    //*/

    // Set RX Bulk-in sizes -- use USB 3.0/1000Mbps at this point
    status = ax88179_configure_bulk_in(eth, AX88179_PLSR_USB_SS|AX88179_PLSR_EPHY_1000);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RQCR, status);
        goto fail;
    }

    // Configure flow control watermark
    data = 0x3c;
    status = ax88179_write_mac(eth, AX88179_MAC_PWLLR, 1, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_PWLLR, status);
        goto fail;
    }
    data = 0x5c;
    status = ax88179_write_mac(eth, AX88179_MAC_PWLHR, 1, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_PWLHR, status);
        goto fail;
    }

    // RX/TX checksum offload: ipv4, tcp, udp, tcpv6, udpv6
    data = (1<<6) | (1<<5) | (1<<2) | (1<<1) | (1<<0);
    status = ax88179_write_mac(eth, AX88179_MAC_CRCR, 1, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CRCR, status);
        goto fail;
    }
    status = ax88179_write_mac(eth, AX88179_MAC_CTCR, 1, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CTCR, status);
        goto fail;
    }

    // TODO: PHY LED

    // PHY auto-negotiation
    uint16_t phy_data = 0;
    status = ax88179_read_phy(eth, AX88179_PHY_BMCR, &phy_data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_read_phy to %#x failed: %d\n", AX88179_PHY_BMCR, status);
        goto fail;
    }
    phy_data |= 0x1200;
    status = ax88179_write_phy(eth, AX88179_PHY_BMCR, phy_data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_phy to %#x failed: %d\n", AX88179_PHY_BMCR, status);
        goto fail;
    }

    // Default Ethernet medium mode
    data = 0x013b;
    status = ax88179_write_mac(eth, AX88179_MAC_MSR, 2, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_MSR, status);
        goto fail;
    }

    // Enable MAC RX
    data = 0x039a;
    status = ax88179_write_mac(eth, AX88179_MAC_RCR, 2, &data);
    if (status < 0) {
        dprintf(ERROR, "ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RCR, status);
        goto fail;
    }

    // Create the device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ax88179",
        .ctx = eth,
        .ops = &ax88179_device_proto,
        .proto_id = MX_PROTOCOL_ETHERMAC,
        .proto_ops = &ethmac_ops,
    };

    status = device_add(eth->usb_device, &args, &eth->device);
    if (status < 0) {
        dprintf(ERROR, "ax88179: failed to create device: %d\n", status);
        goto fail;
    }

    uint64_t count = 0;
    iotxn_t* txn = eth->interrupt_req;
    while (true) {
        completion_reset(&eth->completion);
        iotxn_queue(eth->usb_device, txn);
        completion_wait(&eth->completion, MX_TIME_INFINITE);
        if (txn->status != MX_OK) {
            return txn->status;
        }
        count++;
        ax88179_handle_interrupt(eth, txn);
#if AX88179_DEBUG_VERBOSE
        if (count % 32 == 0) {
            ax88179_dump_regs(eth);
        }
#endif
    }

fail:
    ax88179_free(eth);
    return status;
}

static mx_status_t ax88179_bind(void* ctx, mx_device_t* device, void** cookie) {
    dprintf(TRACE, "ax88179_bind\n");

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
        dprintf(ERROR, "ax88179_bind could not find endpoints\n");
        return MX_ERR_NOT_SUPPORTED;
    }

    ax88179_t* eth = calloc(1, sizeof(ax88179_t));
    if (!eth) {
        dprintf(ERROR, "Not enough memory for ax88179_t\n");
        return MX_ERR_NO_MEMORY;
    }

    list_initialize(&eth->free_read_reqs);
    list_initialize(&eth->free_write_reqs);
    list_initialize(&eth->pending_tx);
    mtx_init(&eth->tx_lock, mtx_plain);
    mtx_init(&eth->mutex, mtx_plain);

    eth->usb_device = device;
    memcpy(&eth->usb, &usb, sizeof(eth->usb));
    eth->bulk_in_addr = bulk_in_addr;
    eth->bulk_out_addr = bulk_out_addr;

    mx_status_t status = MX_OK;
    for (int i = 0; i < READ_REQ_COUNT; i++) {
        iotxn_t* req = usb_alloc_iotxn(bulk_in_addr, USB_BUF_SIZE);
        if (!req) {
            status = MX_ERR_NO_MEMORY;
            goto fail;
        }
        req->length = USB_BUF_SIZE;
        req->complete_cb = ax88179_read_complete;
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
        req->complete_cb = ax88179_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }
    iotxn_t* int_req = usb_alloc_iotxn(intr_addr, INTR_REQ_SIZE);
    if (!int_req) {
        status = MX_ERR_NO_MEMORY;
        goto fail;
    }
    int_req->length = INTR_REQ_SIZE;
    int_req->complete_cb = ax88179_interrupt_complete;
    int_req->cookie = eth;
    eth->interrupt_req = int_req;

    /* This is not needed, as long as the xhci stack does it for us.
    status = usb_set_configuration(device, 1);
    if (status < 0) {
        dprintf(ERROR, "aax88179_bind could not set configuration: %d\n", status);
        return MX_ERR_NOT_SUPPORTED;
    }
    */

    int ret = thrd_create_with_name(&eth->thread, ax88179_thread, eth, "ax88179_thread");
    if (ret != thrd_success) {
        goto fail;
    }
    return MX_OK;

fail:
    dprintf(ERROR, "ax88179_bind failed: %d\n", status);
    ax88179_free(eth);
    return status;
}

static mx_driver_ops_t ax88179_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ax88179_bind,
};

MAGENTA_DRIVER_BEGIN(ethernet_ax88179, ax88179_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, ASIX_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, AX88179_PID),
MAGENTA_DRIVER_END(ethernet_ax88179)
