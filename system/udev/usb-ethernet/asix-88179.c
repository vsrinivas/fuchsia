// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/ethernet.h>
#include <hexdump/hexdump.h>
#include <magenta/listnode.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "asix-88179.h"

#define AX88179_DEBUG 0
#define AX88179_DEBUG_VERBOSE 0
#if AX88179_DEBUG
#  define xprintf(args...) printf(args)
#else
#  define xprintf(args...)
#endif

// borrowed from LK/magenta stdlib.h
#define ROUNDUP(a, b) (((a)+ ((b)-1)) & ~((b)-1))
#define ALIGN(a, b) ROUNDUP(a, b)

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define USB_BUF_SIZE 24576
#define INTR_REQ_SIZE 8
#define RX_HEADER_SIZE 4

typedef struct {
    mx_device_t device;
    mx_device_t* usb_device;
    mx_driver_t* driver;

    uint8_t mac_addr[6];
    uint8_t status[INTR_REQ_SIZE];
    bool online;
    bool dead;

    // interrupt in request
    iotxn_t* interrupt_req;
    completion_t completion;

    // pool of free USB bulk requests
    list_node_t free_read_reqs;
    list_node_t free_write_reqs;

    // list of received packets not yet read by upper layer
    list_node_t completed_reads;
    // index of next packet header to process
    size_t packet;
    // offset of next packet to process from completed_reads head
    size_t read_offset;

    // the last signals we reported
    mx_signals_t signals;

    mtx_t mutex;
} ax88179_t;
#define get_ax88179(dev) containerof(dev, ax88179_t, device)

typedef struct {
    uint16_t num_pkts;
    uint16_t pkt_hdr_off;
} ax88179_rx_hdr_t;

typedef struct {
    uint16_t tx_len;
    uint16_t unused[3];
    // TODO: support additional tx header fields
} ax88179_tx_hdr_t;

static void update_signals_locked(ax88179_t* eth) {
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

static mx_status_t ax88179_read_mac(ax88179_t* eth, uint8_t reg_addr, uint8_t reg_len, void* data) {
    mx_status_t status = usb_control(eth->usb_device, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
            AX88179_REQ_MAC, reg_addr, reg_len, data, reg_len);
#if AX88179_DEBUG
    printf("read mac %#x:\n", reg_addr);
    if (status > 0) {
        hexdump8(data, status);
    }
#endif
    return status;
}

static mx_status_t ax88179_write_mac(ax88179_t* eth, uint8_t reg_addr, uint8_t reg_len, void* data) {
#if AX88179_DEBUG
    printf("write mac %#x:\n", reg_addr);
    hexdump8(data, reg_len);
#endif
    return usb_control(eth->usb_device, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
            AX88179_REQ_MAC, reg_addr, reg_len, data, reg_len);
}

static mx_status_t ax88179_read_phy(ax88179_t* eth, uint8_t reg_addr, uint16_t* data) {
    mx_status_t status = usb_control(eth->usb_device, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
            AX88179_REQ_PHY, AX88179_PHY_ID, reg_addr, data, sizeof(*data));
#if AX88179_DEBUG
    if (status == sizeof(*data)) {
        printf("read phy %#x: %#x\n", reg_addr, *data);
    }
#endif
    return status;
}

static mx_status_t ax88179_write_phy(ax88179_t* eth, uint8_t reg_addr, uint16_t data) {
#if AX88179_DEBUG
    printf("write phy %#x: %#x\n", reg_addr, data);
#endif
    return usb_control(eth->usb_device, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
            AX88179_REQ_PHY, AX88179_PHY_ID, reg_addr, &data, sizeof(data));
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
        printf("ax88179: invalid usb mode: %#x\n", usb_mode);
        return ERR_INVALID_ARGS;
    }

    uint8_t speed = plsr & AX88179_PLSR_EPHY_MASK;
    if (speed & (speed-1)) {
        printf("ax88179: invalid eth speed: %#x\n", speed);
    }

    mx_status_t status = ax88179_write_mac(eth, AX88179_MAC_RQCR, 5,
            ax88179_bulk_in_config[usb_mode][speed >> 4]);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RQCR, status);
    }
    return status;
}

static mx_status_t ax88179_configure_medium_mode(ax88179_t* eth) {
    uint16_t data = 0;
    mx_status_t status = ax88179_read_phy(eth, AX88179_PHY_PHYSR, &data);
    if (status < 0) {
        printf("ax88179_read_phy to %#x failed: %d\n", AX88179_PHY_PHYSR, status);
        return status;
    }

    unsigned int mode = (data & (AX88179_PHYSR_SPEED|AX88179_PHYSR_DUPLEX)) >> 13;
    xprintf("ax88179 medium mode: %#x\n", mode);
    if (mode == 4 || mode > 5) {
        printf("ax88179 mode invalid\n");
        return ERR_NOT_SUPPORTED;
    }
    status = ax88179_write_mac(eth, AX88179_MAC_MSR, 2, ax88179_media_mode[mode]);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_MSR, status);
        return status;
    }

    data = 0;
    status = ax88179_read_mac(eth, AX88179_MAC_PLSR, 1, &data);
    if (status < 0) {
        printf("ax88179_read_mac to %#x failed: %d\n", AX88179_MAC_PLSR, status);
        return status;
    }
    status = ax88179_configure_bulk_in(eth, data & 0xff);

    return status;
}

static void requeue_read_request_locked(ax88179_t* eth, iotxn_t* req) {
    if (eth->online) {
        iotxn_queue(eth->usb_device, req);
    }
}

static void ax88179_read_complete(iotxn_t* request, void* cookie) {
    ax88179_t* eth = (ax88179_t*)cookie;

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

static void ax88179_write_complete(iotxn_t* request, void* cookie) {
    ax88179_t* eth = (ax88179_t*)cookie;

    if (request->status == ERR_REMOTE_CLOSED) {
        request->ops->release(request);
        return;
    }

    mtx_lock(&eth->mutex);
    list_add_tail(&eth->free_write_reqs, &request->node);
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
}

static void ax88179_interrupt_complete(iotxn_t* request, void* cookie) {
    if (request->status == ERR_REMOTE_CLOSED) {
        request->ops->release(request);
        return;
    }

    ax88179_t* eth = (ax88179_t*)cookie;
    completion_signal(&eth->completion);
}

static void ax88179_handle_interrupt(ax88179_t* eth, iotxn_t* request) {
    mtx_lock(&eth->mutex);
    if (request->status == NO_ERROR && request->actual == sizeof(eth->status)) {
        uint8_t status[INTR_REQ_SIZE];

        request->ops->copyfrom(request, status, sizeof(status), 0);
        if (memcmp(eth->status, status, sizeof(eth->status))) {
#if AX88179_DEBUG
            const uint8_t* b = status;
            printf("ax88179 status changed: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                   b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
#endif
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
                    requeue_read_request_locked(eth, req);
                }
                update_signals_locked(eth);
                xprintf("ax88179 now online\n");
            } else if (!online && was_online) {
                update_signals_locked(eth);
                xprintf("ax88179 now offline\n");
            }
        }
    }

    mtx_unlock(&eth->mutex);
}

mx_status_t ax88179_send(mx_device_t* device, const void* buffer, size_t length) {
    ax88179_t* eth = get_ax88179(device);

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

    if (length + sizeof(ax88179_tx_hdr_t) > 1500) {
        status = ERR_INVALID_ARGS;
        goto out;
    }

    ax88179_tx_hdr_t txhdr;
    memset(&txhdr, 0, sizeof(txhdr));
    txhdr.tx_len = htole16(length);
    request->ops->copyto(request, &txhdr, sizeof(txhdr), 0);
    request->ops->copyto(request, buffer, length, sizeof(txhdr));
    request->length = length + sizeof(txhdr);
    iotxn_queue(eth->usb_device, request);

out:
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
    return status;
}

mx_status_t ax88179_recv(mx_device_t* device, void* buffer, size_t length) {
    ax88179_t* eth = get_ax88179(device);

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
    xprintf("request len %llu\n", request->actual);

    if (request->actual < 4) {
        printf("ax88179_recv short packet\n");
        status = ERR_INTERNAL;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
        goto out;
    }
    ax88179_rx_hdr_t rxhdr;
    request->ops->copyfrom(request, (void*)&rxhdr, RX_HEADER_SIZE, request->actual - sizeof(rxhdr));
    xprintf("rxhdr offset %u, num %u\n", rxhdr.pkt_hdr_off, rxhdr.num_pkts);
    if (rxhdr.num_pkts < 1 ||
        rxhdr.pkt_hdr_off >= request->actual - sizeof(rxhdr)) {
        status = ERR_IO_DATA_INTEGRITY;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
        goto out;
    }

    xprintf("next packet: %zd\n", eth->packet);
    uint32_t pkt_hdr;
    ptrdiff_t pkt_idx = eth->packet++ * sizeof(pkt_hdr);
    request->ops->copyfrom(request, (void*)&pkt_hdr, sizeof(pkt_hdr), rxhdr.pkt_hdr_off + pkt_idx);
    uint16_t pkt_len = le16toh((pkt_hdr & AX88179_RX_PKTLEN) >> 16);
    xprintf("pkt_hdr: %0#x pkt_len: %u\n", pkt_hdr, pkt_len);
    if (pkt_len > length) {
        status = ERR_BUFFER_TOO_SMALL;
        goto out;
    }
    xprintf("offset = %zd\n", offset);
    request->ops->copyfrom(request, buffer, pkt_len - 2, offset + 2);
    status = pkt_len - 2;

    offset += pkt_len;
    offset = ALIGN(offset, 8);
    if (offset >= rxhdr.pkt_hdr_off) {
        offset = 0;
        eth->packet = 0;
        list_remove_head(&eth->completed_reads);
        requeue_read_request_locked(eth, request);
    }

out:
#if AX88179_DEBUG
    if (offset != 0) {
        printf("setting read offset to %zd\n", offset);
    }
#endif
    eth->read_offset = offset;

    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
    return status;
}

mx_status_t ax88179_get_mac_addr(mx_device_t* device, uint8_t* out_addr) {
    ax88179_t* eth = get_ax88179(device);
    memcpy(out_addr, eth->mac_addr, sizeof(eth->mac_addr));
    return NO_ERROR;
}

bool ax88179_is_online(mx_device_t* device) {
    ax88179_t* eth = get_ax88179(device);
    return eth->online;
}

size_t ax88179_get_mtu(mx_device_t* device) {
    return 1500;
    // TODO: figure this out
    //return USB_BUF_SIZE - ETH_HEADER_SIZE;
}

static ethernet_protocol_t ax88179_proto = {
    .send = ax88179_send,
    .recv = ax88179_recv,
    .get_mac_addr = ax88179_get_mac_addr,
    .is_online = ax88179_is_online,
    .get_mtu = ax88179_get_mtu,
};

static void ax88179_unbind(mx_device_t* device) {
    ax88179_t* eth = get_ax88179(device);
    device_remove(&eth->device);

    mtx_lock(&eth->mutex);
    eth->dead = true;
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);
}

static mx_status_t ax88179_release(mx_device_t* device) {
    ax88179_t* eth = get_ax88179(device);

    iotxn_t* txn;
    while ((txn = list_remove_head_type(&eth->free_read_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    eth->interrupt_req->ops->release(eth->interrupt_req);

    free(eth);
    return NO_ERROR;
}

// simplified read/write interface

static ssize_t eth_read(mx_device_t* dev, void* data, size_t len, mx_off_t off) {
    // special case reading MAC address
    if (len == ETH_MAC_SIZE) {
        ax88179_get_mac_addr(dev, data);
        return len;
    }
    if (len < ax88179_get_mtu(dev)) {
        xprintf("%s: ERR_BUFFER_TOO_SMALL\n", __func__);
        return ERR_BUFFER_TOO_SMALL;
    }
    return ax88179_recv(dev, data, len);
}

static ssize_t eth_write(mx_device_t* dev, const void* data, size_t len, mx_off_t off) {
    return ax88179_send(dev, data, len);
}

static mx_protocol_device_t ax88179_device_proto = {
    .unbind = ax88179_unbind,
    .release = ax88179_release,
    .read = eth_read,
    .write = eth_write,
};

#define READ_REG(r, len) \
    do { \
        reg = 0; \
        mx_status_t status = ax88179_read_mac(eth, r, len, &reg); \
        if (status < 0) { \
            printf("ax88179: could not read reg " #r ": %d\n", status); \
        } else { \
            printf("ax88179: reg " #r " = %" PRIx64 "\n", reg); \
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
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_EPPRCR, status);
        goto fail;
    }
    mx_nanosleep(MX_MSEC(1));
    data = 0x0020;
    status = ax88179_write_mac(eth, AX88179_MAC_EPPRCR, 2, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_EPPRCR, status);
        goto fail;
    }
    mx_nanosleep(MX_MSEC(200));

    // Switch clock to normal speed
    data = 0x03;
    status = ax88179_write_mac(eth, AX88179_MAC_CLKSR, 1, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CLKSR, status);
        goto fail;
    }
    mx_nanosleep(MX_MSEC(1));

    // Read the MAC addr
    status = ax88179_read_mac(eth, AX88179_MAC_NIDR, 6, eth->mac_addr);
    if (status < 0) {
        printf("ax88179_read_mac to %#x failed: %d\n", AX88179_MAC_NIDR, status);
        goto fail;
    }

    printf("ax88179 MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           eth->mac_addr[0], eth->mac_addr[1], eth->mac_addr[2],
           eth->mac_addr[3], eth->mac_addr[4], eth->mac_addr[5]);

    ///* Disabled for now
    // Ensure that the MAC RX is disabled
    data = 0;
    status = ax88179_write_mac(eth, AX88179_MAC_RCR, 2, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RCR, status);
        goto fail;
    }
    //*/

    // Set RX Bulk-in sizes -- use USB 3.0/1000Mbps at this point
    status = ax88179_configure_bulk_in(eth, AX88179_PLSR_USB_SS|AX88179_PLSR_EPHY_1000);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RQCR, status);
        goto fail;
    }

    // Configure flow control watermark
    data = 0x3c;
    status = ax88179_write_mac(eth, AX88179_MAC_PWLLR, 1, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_PWLLR, status);
        goto fail;
    }
    data = 0x5c;
    status = ax88179_write_mac(eth, AX88179_MAC_PWLHR, 1, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_PWLHR, status);
        goto fail;
    }

    // RX/TX checksum offload: ipv4, tcp, udp, tcpv6, udpv6
    data = (1<<6) | (1<<5) | (1<<2) | (1<<1) | (1<<0);
    status = ax88179_write_mac(eth, AX88179_MAC_CRCR, 1, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CRCR, status);
        goto fail;
    }
    status = ax88179_write_mac(eth, AX88179_MAC_CTCR, 1, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_CTCR, status);
        goto fail;
    }

    // TODO: PHY LED

    // PHY auto-negotiation
    uint16_t phy_data = 0;
    status = ax88179_read_phy(eth, AX88179_PHY_BMCR, &phy_data);
    if (status < 0) {
        printf("ax88179_read_phy to %#x failed: %d\n", AX88179_PHY_BMCR, status);
        goto fail;
    }
    phy_data |= 0x1200;
    status = ax88179_write_phy(eth, AX88179_PHY_BMCR, phy_data);
    if (status < 0) {
        printf("ax88179_write_phy to %#x failed: %d\n", AX88179_PHY_BMCR, status);
        goto fail;
    }

    // Default Ethernet medium mode
    data = 0x013b;
    status = ax88179_write_mac(eth, AX88179_MAC_MSR, 2, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_MSR, status);
        goto fail;
    }

    // Enable MAC RX
    data = 0x0398;
    status = ax88179_write_mac(eth, AX88179_MAC_RCR, 2, &data);
    if (status < 0) {
        printf("ax88179_write_mac to %#x failed: %d\n", AX88179_MAC_RCR, status);
        goto fail;
    }

    device_init(&eth->device, eth->driver, "ax88179", &ax88179_device_proto);

    eth->device.protocol_id = MX_PROTOCOL_ETHERNET;
    eth->device.protocol_ops = &ax88179_proto;
    status = device_add(&eth->device, eth->usb_device);
    if (status != NO_ERROR) {
        goto fail;
    }

    uint64_t count = 0;
    iotxn_t* txn = eth->interrupt_req;
    while (true) {
        completion_reset(&eth->completion);
        iotxn_queue(eth->usb_device, txn);
        completion_wait(&eth->completion, MX_TIME_INFINITE);
        if (txn->status != NO_ERROR) {
            break;
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
    ax88179_release(&eth->device);
    return status;
}

static mx_status_t ax88179_bind(mx_driver_t* driver, mx_device_t* device) {
    xprintf("ax88179_bind\n");
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
        printf("ax88179_bind could not find endpoints\n");
        return ERR_NOT_SUPPORTED;
    }

    ax88179_t* eth = calloc(1, sizeof(ax88179_t));
    if (!eth) {
        printf("Not enough memory for ax88179_t\n");
        return ERR_NO_MEMORY;
    }

    list_initialize(&eth->free_read_reqs);
    list_initialize(&eth->free_write_reqs);
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
        req->complete_cb = ax88179_read_complete;
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
        req->complete_cb = ax88179_write_complete;
        req->cookie = eth;
        list_add_head(&eth->free_write_reqs, &req->node);
    }
    iotxn_t* int_req = usb_alloc_iotxn(intr_addr, INTR_REQ_SIZE, 0);
    if (!int_req) {
        status = ERR_NO_MEMORY;
        goto fail;
    }
    int_req->length = INTR_REQ_SIZE;
    int_req->complete_cb = ax88179_interrupt_complete;
    int_req->cookie = eth;
    eth->interrupt_req = int_req;

    /* This is not needed, as long as the xhci stack does it for us.
    status = usb_set_configuration(device, 1);
    if (status < 0) {
        printf("aax88179_bind could not set configuration: %d\n", status);
        return ERR_NOT_SUPPORTED;
    }
    */

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, ax88179_thread, eth, "ax88179_thread");
    if (ret != thrd_success) {
        goto fail;
    }
    thrd_detach(thread);
    return NO_ERROR;

fail:
    printf("ax88179_bind failed: %d\n", status);
    ax88179_release(&eth->device);
    return status;
}

mx_driver_t _driver_ax88179 = {
    .ops = {
        .bind = ax88179_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_ax88179, "usb-ethernet-ax88179", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, ASIX_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, AX88179_PID),
MAGENTA_DRIVER_END(_driver_ax88179)
