// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/block.h>
#include "ahci.h"

#define SATA_CMD_IDENTIFY_DEVICE      0xec
#define SATA_CMD_READ_DMA             0xc8
#define SATA_CMD_READ_DMA_EXT         0x25
#define SATA_CMD_READ_FPDMA_QUEUED    0x60
#define SATA_CMD_WRITE_DMA            0xca
#define SATA_CMD_WRITE_DMA_EXT        0x35
#define SATA_CMD_WRITE_FPDMA_QUEUED   0x61

#define SATA_DEVINFO_SERIAL              10
#define SATA_DEVINFO_FW_REV              23
#define SATA_DEVINFO_MODEL_ID            27
#define SATA_DEVINFO_CAP                 49
#define SATA_DEVINFO_LBA_CAPACITY        60
#define SATA_DEVINFO_QUEUE_DEPTH         75
#define SATA_DEVINFO_SATA_CAP            76
#define SATA_DEVINFO_SATA_CAP2           77
#define SATA_DEVINFO_MAJOR_VERS          80
#define SATA_DEVINFO_CMD_SET_2           83
#define SATA_DEVINFO_LBA_CAPACITY_2      100
#define SATA_DEVINFO_SECTOR_SIZE         106
#define SATA_DEVINFO_LOGICAL_SECTOR_SIZE 117

#define SATA_DEVINFO_SERIAL_LEN   20
#define SATA_DEVINFO_FW_REV_LEN   8
#define SATA_DEVINFO_MODEL_ID_LEN 40

#define SATA_MAX_BLOCK_COUNT  0x10000 // 16-bit count

#define BLOCK_OP(op) ((op) & BLOCK_OP_MASK)

typedef struct sata_txn {
    block_op_t bop;
    list_node_t node;

    zx_time_t timeout;

    uint8_t cmd;
    uint8_t device;

    zx_status_t status;
    zx_handle_t pmt;
} sata_txn_t;

typedef struct ahci_device ahci_device_t;

typedef struct sata_devinfo {
    uint32_t block_size;
    int max_cmd;
} sata_devinfo_t;

zx_status_t sata_bind(ahci_device_t* controller, zx_device_t* parent, int port);

// sets the device info for the device at portnr
void ahci_set_devinfo(ahci_device_t* controller, int portnr, sata_devinfo_t* devinfo);

// queue a txn on the controller
void ahci_queue(ahci_device_t* controller, int portnr, sata_txn_t* txn);

static inline void block_complete(block_op_t* bop, zx_status_t status) {
    bop->completion_cb(bop, status);
}
