// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "ahci.h"

#define SATA_CMD_IDENTIFY_DEVICE 0xec
#define SATA_CMD_READ_DMA        0xc8
#define SATA_CMD_READ_DMA_EXT    0x25
#define SATA_CMD_WRITE_DMA       0xca
#define SATA_CMD_WRITE_DMA_EXT   0x35

#define SATA_DEVINFO_SERIAL              10
#define SATA_DEVINFO_FW_REV              23
#define SATA_DEVINFO_MODEL_ID            27
#define SATA_DEVINFO_CAP                 49
#define SATA_DEVINFO_LBA_CAPACITY        60
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
    uint64_t lba;   // in blocks
    uint16_t count; // in blocks
    uint8_t cmd;
    uint8_t device;
    int port;
} sata_pdata_t;

#define sata_iotxn_pdata(txn) iotxn_pdata(txn, sata_pdata_t)

mx_status_t sata_bind(mx_device_t* dev, int port);
