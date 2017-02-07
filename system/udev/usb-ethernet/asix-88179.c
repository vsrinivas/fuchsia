// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/ethernet.h>
#include <eth/eth-fifo.h>
#include <hexdump/hexdump.h>
#include <magenta/device/ethernet.h>
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
#define AX88179_MTU 1500
#define MAX_ETH_HDRS 26

typedef struct {
    mx_device_t* device;
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

    // list of received packets not yet queued into the rx fifo
    list_node_t completed_reads;
    // index of next packet header to process
    size_t packet;
    // offset of next packet to process from completed_reads head
    size_t read_offset;

    // fifos
    completion_t rx_complete;
    eth_fifo_t fifo;
    eth_fifo_entry_t* rx_entries;
    eth_fifo_entry_t* tx_entries;
    // Buffer VMO
    mx_handle_t io_vmo;
    // Buffer mappings
    uint8_t* rx_map;
    uint8_t* tx_map;
    // fifo threads
    thrd_t rx_thr;
    thrd_t tx_thr;

    // the last signals we reported
    mx_signals_t signals;

    mtx_t mutex;
} ax88179_t;
#define get_ax88179(dev) ((ax88179_t*)dev->ctx)

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
        device_state_set_clr(eth->device, new_signals & ~eth->signals, eth->signals & ~new_signals);
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
        completion_signal(&eth->rx_complete);
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
        // request will be released in ax88179_release()
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

static mx_status_t process_one_packet(ax88179_t* eth, iotxn_t* completed_read,
        mx_fifo_state_t* fifo_state) {
    xprintf("request len %" PRIu64"\n", completed_read->actual);
    size_t offset = eth->read_offset;

    uint64_t entry_idx = fifo_state->tail & (eth->fifo.rx_entries_count - 1);
    eth_fifo_entry_t* entry = &eth->rx_entries[entry_idx];

    if (completed_read->actual < 4) {
        printf("ax88179_recv short packet\n");
        return ERR_INTERNAL;
    }

    uint8_t* read_data = NULL;
    completed_read->ops->mmap(completed_read, (void*)&read_data);

    ptrdiff_t rxhdr_off = completed_read->actual - sizeof(ax88179_rx_hdr_t);
    ax88179_rx_hdr_t* rxhdr = (ax88179_rx_hdr_t*)(read_data + rxhdr_off);
    xprintf("rxhdr offset %u, num %u\n", rxhdr->pkt_hdr_off, rxhdr->num_pkts);
    if (rxhdr->num_pkts < 1 || rxhdr->pkt_hdr_off >= rxhdr_off) {
        printf("%s bad packet\n", __func__);
        return ERR_IO_DATA_INTEGRITY;
    }

    xprintf("next packet: %zd\n", eth->packet);
    ptrdiff_t pkt_idx = eth->packet++ * sizeof(uint32_t);
    uint32_t* pkt_hdr = (uint32_t*)(read_data + rxhdr->pkt_hdr_off + pkt_idx);
    if ((uintptr_t)pkt_hdr >= (uintptr_t)(read_data + completed_read->actual)) {
        printf("%s packet header out of bounds %p > %p\n", __func__, pkt_hdr,
                read_data + completed_read->actual);
        return ERR_IO_DATA_INTEGRITY;
    }
    uint16_t pkt_len = le16toh((*pkt_hdr & AX88179_RX_PKTLEN) >> 16);
    xprintf("pkt_hdr: %0#x pkt_len: %u\n", *pkt_hdr, pkt_len);
    bool drop = false;
    if (*pkt_hdr & AX88179_RX_DROPPKT) {
        xprintf("%s DropPkt\n", __func__);
        drop = true;
    }
    if (*pkt_hdr & AX88179_RX_MIIER) {
        xprintf("%s MII-Er\n", __func__);
        drop = true;
    }
    if (*pkt_hdr & AX88179_RX_CRCER) {
        xprintf("%s CRC-Er\n", __func__);
        drop = true;
    }
    if (!(*pkt_hdr & AX88179_RX_OK)) {
        xprintf("%s !GoodPkt\n", __func__);
        drop = true;
    }
    if (!drop) {
        if (pkt_len > entry->length) {
            // this should never happen? TODO: figure out how to prevent this
            // earlier
            printf("%s packet bigger than rx fifo entry length!!!\n", __func__);
            return ERR_BUFFER_TOO_SMALL;
        }
        xprintf("offset = %zd\n", offset);

        // Write the packet
        memcpy(&eth->rx_map[entry->offset], (void*)&read_data[offset + 2], pkt_len - 2);
        // Update the fifo entry
        entry->length = pkt_len - 2;
        // Move the fifo pointer
        mx_status_t status = mx_fifo_op(eth->fifo.rx_fifo, MX_FIFO_OP_ADVANCE_TAIL, 1u, fifo_state);
        if (status != NO_ERROR) {
            printf("%s could not advance rx fifo tail (%d)\n", __func__, status);
        }
    }

    // Advance past this packet in the completed read
    offset += pkt_len;
    offset = ALIGN(offset, 8);
    if (offset >= rxhdr->pkt_hdr_off) {
        offset = 0;
        eth->packet = 0;
    }

#if AX88179_DEBUG
    if (offset != 0) {
        printf("setting read offset to %zd\n", offset);
    }
#endif
    eth->read_offset = offset;

    return NO_ERROR;
}

static int ax88179_rx_thread(void* arg) {
    ax88179_t* eth = (ax88179_t*)arg;

    mx_fifo_state_t state;
    mx_status_t status;
    while (true) {
        // Check every second if we're being unbound
        status = completion_wait(&eth->rx_complete, MX_SEC(1));
        if (eth->dead) {
            break;
        }
        if (status == ERR_TIMED_OUT) {
            mtx_lock(&eth->mutex);
            if (list_is_empty(&eth->completed_reads)) {
                mtx_unlock(&eth->mutex);
                continue;
            }
            mtx_unlock(&eth->mutex);
        }

        // Drain the completed reads, waiting for room in the rx fifo as needed
        mtx_lock(&eth->mutex);
        list_node_t* node = list_remove_head(&eth->completed_reads);
        // Don't have a fifo yet; just drop the read
        if (eth->fifo.entries_vmo == MX_HANDLE_INVALID) {
            xprintf("%s no fifo in place yet, dropping packet\n", __func__);
            completion_reset(&eth->rx_complete);
            requeue_read_request_locked(eth, containerof(node, iotxn_t, node));
            mtx_unlock(&eth->mutex);
            continue;
        }
        mtx_unlock(&eth->mutex);

        while (node) {
            // TODO: we should probably drop packets if the rx fifo is full
            do {
                status = mx_handle_wait_one(eth->fifo.rx_fifo, MX_FIFO_NOT_EMPTY, MX_SEC(1), NULL);
                if (eth->dead) return 0;
            } while (status == ERR_TIMED_OUT);
            if (status != NO_ERROR) {
                break;
            }

            // Find out how much room we have.
            status = mx_fifo_op(eth->fifo.rx_fifo, MX_FIFO_OP_READ_STATE, 0, &state);
            if (status != NO_ERROR) {
                printf("%s could not read rx fifo state (%d)\n", __func__, status);
                break;
            }
            if (state.head == state.tail) {
                printf("%s head == tail!!!\n", __func__);
                // this shouldn't happen, since we found MX_FIFO_NOT_EMPTY,
                // and we're the only one that will move tail
                break;
            }
            uint64_t num_fifo_entries = state.head - state.tail;
            iotxn_t* request = containerof(node, iotxn_t, node);
            while (num_fifo_entries > 0) {
                status = process_one_packet(eth, request, &state);
                if (status != NO_ERROR) {
                    printf("%s could not process packet (%d)\n", __func__, status);
                    // error???
                }
                if (eth->read_offset == 0) {
                    // Get the next completed read txn, if any
                    mtx_lock(&eth->mutex);
                    requeue_read_request_locked(eth, request);
                    node = list_remove_head(&eth->completed_reads);
                    if (!node) {
                        update_signals_locked(eth);
                        completion_reset(&eth->rx_complete);
                        mtx_unlock(&eth->mutex);
                        goto wait_for_rx;
                    }
                    mtx_unlock(&eth->mutex);
                    request = containerof(node, iotxn_t, node);
                }

                num_fifo_entries = state.head - state.tail;
            }
        }
wait_for_rx: ;
    }
    return 0;
}

static mx_status_t send_one_packet(ax88179_t* eth, iotxn_t* request, mx_fifo_state_t *fifo_state) {
    uint64_t entry_idx = fifo_state->tail & (eth->fifo.tx_entries_count - 1);
    eth_fifo_entry_t* entry = &eth->tx_entries[entry_idx];
    if (entry->length > AX88179_MTU + MAX_ETH_HDRS) {
        printf("%s tx entry too large (%u > %u)\n", __func__, entry->length, AX88179_MTU + MAX_ETH_HDRS);
        return ERR_BUFFER_TOO_SMALL;
    }

    uint8_t* write_buffer = NULL;
    request->ops->mmap(request, (void*)&write_buffer);

    ax88179_tx_hdr_t* txhdr = (ax88179_tx_hdr_t*)write_buffer;
    memset(txhdr, 0, sizeof(*txhdr));
    txhdr->tx_len = htole16(entry->length);

    memcpy((void*)(write_buffer + sizeof(*txhdr)), (void*)&eth->tx_map[entry->offset],
            entry->length);
    request->length = entry->length + sizeof(txhdr);
    iotxn_queue(eth->usb_device, request);

    // Move the fifo pointer
    // TODO: batch these up
    mx_status_t status = mx_fifo_op(eth->fifo.tx_fifo, MX_FIFO_OP_ADVANCE_TAIL, 1u, fifo_state);
    if (status != NO_ERROR) {
        printf("%s could not advance tx fifo tail (%d)\n", __func__, status);
    }

    return NO_ERROR;
}

static int ax88179_tx_thread(void* arg) {
    ax88179_t* eth = (ax88179_t*)arg;

    mx_fifo_state_t state;
    mx_status_t status;
    while (true) {
        do {
            status = mx_handle_wait_one(eth->fifo.tx_fifo, MX_FIFO_NOT_EMPTY, MX_SEC(1), NULL);
            if (eth->dead) {
                return 0;
            }
        } while (status == ERR_TIMED_OUT);
        if (status != NO_ERROR) {
            printf("%s handle wait for tx fifo failed (%d)\n", __func__, status);
            break;
        }

        status = mx_fifo_op(eth->fifo.tx_fifo, MX_FIFO_OP_READ_STATE, 0, &state);
        if (status != NO_ERROR) {
            printf("%s could not read tx fifo state (%d)\n", __func__, status);
            break;
        }
        if (state.head == state.tail) continue;

        uint64_t num_fifo_entries = state.head - state.tail;
        while (num_fifo_entries > 0) {
            mtx_lock(&eth->mutex);
            list_node_t* node = list_remove_head(&eth->free_write_reqs);
            mtx_unlock(&eth->mutex);
            if (!node) {
                // drop packets when no outgoing buffers
                // or do we use a completion to find out when there are new
                // buffers?
                break;
            }
            status = send_one_packet(eth, containerof(node, iotxn_t, node), &state);
            if (status != NO_ERROR) {
                printf("%s could not send packet (%d)\n", __func__, status);
                break;
                // error???
            }
            num_fifo_entries = state.head - state.tail;
        }
        mtx_lock(&eth->mutex);
        update_signals_locked(eth);
        mtx_unlock(&eth->mutex);
    }
    return 0;
}

mx_status_t ax88179_get_mac_addr(mx_device_t* device, uint8_t* out_addr) {
    ax88179_t* eth = get_ax88179(device);
    memcpy(out_addr, eth->mac_addr, sizeof(eth->mac_addr));
    return NO_ERROR;
}

size_t ax88179_get_mtu(mx_device_t* device) {
    return AX88179_MTU;
}

static ethernet_protocol_t ax88179_proto = {};

static void ax88179_unbind(mx_device_t* device) {
    ax88179_t* eth = get_ax88179(device);

    mtx_lock(&eth->mutex);
    eth->dead = true;
    completion_signal(&eth->rx_complete);
    update_signals_locked(eth);
    mtx_unlock(&eth->mutex);

    // this must be last since this can trigger releasing the device
    device_remove(eth->device);
}

static void ax88179_free(ax88179_t* eth) {
    iotxn_t* txn;
    while ((txn = list_remove_head_type(&eth->free_read_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&eth->free_write_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    eth->interrupt_req->ops->release(eth->interrupt_req);

    free(eth->device);
    free(eth);
}

static mx_status_t ax88179_release(mx_device_t* device) {
    ax88179_t* eth = get_ax88179(device);
    ax88179_free(eth);
    return NO_ERROR;
}

static ssize_t ax88179_get_fifo(ax88179_t* eth, const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len) {
    if (in_len < sizeof(eth_get_fifo_args_t) || !in_buf) return ERR_INVALID_ARGS;
    if (out_len < sizeof(eth_fifo_t) || !out_buf) return ERR_INVALID_ARGS;
    // For now, we can only have one fifo per instance
    if (eth->fifo.entries_vmo != MX_HANDLE_INVALID) return ERR_ALREADY_BOUND;

    const eth_get_fifo_args_t* args = in_buf;
    eth_fifo_t* reply = out_buf;

    // Create the fifo
    eth_fifo_t fifo;
    mx_status_t status = eth_fifo_create(args->rx_entries, args->tx_entries, args->options,
        &fifo);
    if (status != NO_ERROR) {
        printf("failed to create eth_fifo (%d)\n", status);
        eth_fifo_cleanup(&fifo);
        return status;
    }

    // Set up the driver's copy
    status = eth_fifo_clone_consumer(&fifo, &eth->fifo);
    if (status != NO_ERROR) {
        printf("failed to clone consumer fifo: %d\n", status);
        goto clone_consumer_fail;
    }
    status = eth_fifo_map_rx_entries(&eth->fifo, &eth->rx_entries);
    if (status != NO_ERROR) {
        printf("failed to map rx fifo entries: %d\n", status);
        goto map_rx_entries_fail;
    }
    status = eth_fifo_map_tx_entries(&eth->fifo, &eth->tx_entries);
    if (status != NO_ERROR) {
        printf("failed to map tx fifo entries: %d\n", status);
        goto map_tx_entries_fail;
    }

    // Set up the caller's copy
    status = eth_fifo_clone_producer(&fifo, reply);
    if (status != NO_ERROR) {
        printf("failed to clone producer fifo: %d\n", status);
        goto clone_producer_fail;
    }

    // Spawn the rx/tx threads
    int ret = thrd_create_with_name(&eth->rx_thr, ax88179_rx_thread, eth, "ax88179_rx_thread");
    if (ret != thrd_success) {
        printf("failed to start rx thread: %d\n", ret);
        status = ERR_BAD_STATE;
        goto rx_thread_fail;
    }
    ret = thrd_create_with_name(&eth->tx_thr, ax88179_tx_thread, eth, "ax88179_tx_thread");
    if (ret != thrd_success) {
        printf("failed to start tx thread: %d\n", ret);
        status = ERR_BAD_STATE;
        goto tx_thread_fail;
    }

    thrd_detach(eth->rx_thr);
    thrd_detach(eth->tx_thr);

    return sizeof(*reply);

tx_thread_fail:
    thrd_detach(eth->rx_thr);
rx_thread_fail:
    eth_fifo_cleanup(reply);
clone_producer_fail:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)eth->tx_entries, 0);
map_tx_entries_fail:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)eth->rx_entries, 0);
map_rx_entries_fail:
    eth_fifo_cleanup(&eth->fifo);
clone_consumer_fail:
    eth_fifo_cleanup(&fifo);
    return status;
}

static ssize_t ax88179_set_io_buf(ax88179_t* eth, const void* in_buf, size_t in_len) {
    if (in_len < sizeof(eth_set_io_buf_args_t) || !in_buf) return ERR_INVALID_ARGS;

    const eth_set_io_buf_args_t* args = in_buf;
    eth->io_vmo = args->io_buf_vmo;

    mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, eth->io_vmo, args->rx_offset,
            args->rx_len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)&eth->rx_map);
    if (status != NO_ERROR) {
        printf("ax88179: could not map rx buffer: %d\n", status);
        goto map_rx_failed;
    }

    status = mx_vmar_map(mx_vmar_root_self(), 0, eth->io_vmo, args->tx_offset,
            args->tx_len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, (uintptr_t*)&eth->tx_map);
    if (status != NO_ERROR) {
        printf("ax88179: could not map tx buffer: %d\n", status);
        goto map_tx_failed;
    }
    return NO_ERROR;

map_tx_failed:
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)eth->rx_map, 0);
map_rx_failed:
    mx_handle_close(eth->io_vmo);
    eth->io_vmo = MX_HANDLE_INVALID;
    return status;
}

static ssize_t ax88179_ioctl(mx_device_t* dev, uint32_t op, const void* in_buf, size_t in_len,
        void* out_buf, size_t out_len) {
    ax88179_t* eth = get_ax88179(dev);
    switch (op) {
    case IOCTL_ETHERNET_GET_MAC_ADDR: {
        uint8_t* mac = out_buf;
        if (out_len < ETH_MAC_SIZE) return ERR_BUFFER_TOO_SMALL;
        ax88179_get_mac_addr(dev, mac);
        return ETH_MAC_SIZE;
    }
    case IOCTL_ETHERNET_GET_MTU: {
        size_t* mtu = out_buf;
        if (out_len < sizeof(*mtu)) return ERR_BUFFER_TOO_SMALL;
        *mtu = ax88179_get_mtu(dev);
        return sizeof(*mtu);
    }
    case IOCTL_ETHERNET_GET_FIFO:
        return ax88179_get_fifo(eth, in_buf, in_len, out_buf, out_len);
    case IOCTL_ETHERNET_SET_IO_BUF:
        return ax88179_set_io_buf(eth, in_buf, in_len);
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_protocol_device_t ax88179_device_proto = {
    .ioctl = ax88179_ioctl,
    .unbind = ax88179_unbind,
    .release = ax88179_release,
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

    // Create the device
    status = device_create(&eth->device, eth->driver, "ax88179", &ax88179_device_proto);
    if (status < 0) {
        printf("ax88179: failed to create device: %d\n", status);
        goto fail;
    }

    eth->device->ctx = eth;
    eth->device->protocol_id = MX_PROTOCOL_ETHERNET;
    eth->device->protocol_ops = &ax88179_proto;
    status = device_add(eth->device, eth->usb_device);
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
    ax88179_free(eth);
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
    ax88179_free(eth);
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
