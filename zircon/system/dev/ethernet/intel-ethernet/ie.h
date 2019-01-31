// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include "ie-hw.h"

#define IE_DID_I211_AT 0x1539
#define IE_DID_I219_LM 0x156f

typedef struct framebuf framebuf_t;
typedef struct ethdev ethdev_t;

struct framebuf {
    list_node_t node;
    uintptr_t phys;
    void* data;
    size_t size;
};

struct ethdev {
    uintptr_t iobase;

    // tx/rx descriptor rings
    ie_txd_t* txd;
    ie_rxd_t* rxd;

    uint32_t tx_wr_ptr;
    uint32_t tx_rd_ptr;
    uint32_t rx_rd_ptr;

    list_node_t free_frames;
    list_node_t busy_frames;

    // base physical addresses for
    // tx/rx rings and rx buffers
    // store as 64bit integer to match hw register size
    uint64_t txd_phys;
    uint64_t rxd_phys;
    uint64_t rxb_phys;
    void* rxb;

    uint8_t mac[6];

    uint8_t phy_addr;
    mtx_t send_lock;

    uint16_t pci_did;
};

#define ETH_MTU 1500

#define ETH_RXBUF_SIZE  2048
#define ETH_RXBUF_COUNT 32

#define ETH_TXBUF_SIZE  2048
#define ETH_TXBUF_COUNT 32
#define ETH_TXBUF_HSIZE 128
#define ETH_TXBUF_DSIZE (ETH_TXBUF_SIZE - ETH_TXBUF_HSIZE)

#define ETH_DRING_SIZE 2048

#define ETH_ALLOC ((ETH_RXBUF_SIZE * ETH_RXBUF_COUNT) + \
                   (ETH_TXBUF_SIZE * ETH_TXBUF_COUNT) + \
                   (ETH_DRING_SIZE * 2))

status_t eth_reset_hw(ethdev_t* eth);
void eth_setup_buffers(ethdev_t* eth, void* iomem, uintptr_t iophys);
void eth_init_hw(ethdev_t* eth);

void eth_dump_regs(ethdev_t* eth);

status_t eth_rx(ethdev_t* eth, void** data, size_t* len);
void eth_rx_ack(ethdev_t* eth);
void eth_enable_rx(ethdev_t* eth);
void eth_disable_rx(ethdev_t* eth);

status_t eth_tx(ethdev_t* eth, const void* data, size_t len);
size_t eth_tx_queued(ethdev_t* eth);
void eth_enable_tx(ethdev_t* eth);
void eth_disable_tx(ethdev_t* eth);

void eth_start_promisc(ethdev_t* eth);
void eth_stop_promisc(ethdev_t* eth);

zx_status_t eth_enable_phy(ethdev_t* eth);
zx_status_t eth_disable_phy(ethdev_t* eth);

bool eth_status_online(ethdev_t* eth);

#define ETH_IRQ_RX IE_INT_RXT0
#define ETH_IRQ_LSC IE_INT_LSC
unsigned eth_handle_irq(ethdev_t* eth);
