// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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

typedef struct sata_pdata {
    mx_time_t timeout; // for ahci driver watchdog
    uint64_t lba;   // in blocks
    uint16_t count; // in blocks
    uint8_t cmd;
    uint8_t device;
    int max_cmd;
    int port;
} sata_pdata_t;

#define sata_iotxn_pdata(txn) iotxn_pdata(txn, sata_pdata_t)

mx_status_t sata_bind(mx_device_t* dev, int port);
