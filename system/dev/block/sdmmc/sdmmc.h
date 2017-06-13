// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <hw/sdmmc.h>

__BEGIN_CDECLS;

typedef struct sdmmc {
    mx_device_t* mxdev;
    mx_device_t* host_mxdev;

    unsigned type;

    uint16_t rca;           // Relative address
    uint16_t bus_widths;    // Supported bus widths

    uint64_t capacity;      // Card capacity

    uint32_t raw_cid[4];
    uint32_t raw_csd[4];
    uint8_t raw_ext_csd[512];

    block_callbacks_t* callbacks;
} sdmmc_t;

#define SDMMC_TYPE_SD   0
#define SDMMC_TYPE_MMC  1

// Issue a command to the host controller
mx_status_t sdmmc_do_command(mx_device_t* dev, const uint32_t cmd, const uint32_t arg, iotxn_t* txn);

mx_status_t sdmmc_probe_sd(sdmmc_t* sdmmc, iotxn_t* setup_txn);
mx_status_t sdmmc_probe_mmc(sdmmc_t* sdmmc, iotxn_t* setup_txn);

__END_CDECLS;
