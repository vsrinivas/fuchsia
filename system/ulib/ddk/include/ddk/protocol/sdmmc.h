// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/iotxn.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

// protocol data for iotxns
typedef struct sdmmc_protocol_data {
    uint32_t cmd;  // Command to issue to the underlying device.
    uint32_t arg;  // Argument to accompany the command.

    uint16_t blockcount;   // For IOps, number of blocks to read/write.
    uint16_t blocksize;    // For IOps, size of blocks to read/write.

    uint16_t blockid;      // Current block to transfer in PIO
    uint32_t response[4];  // Response data.
} sdmmc_protocol_data_t;

static_assert(sizeof(sdmmc_protocol_data_t) <= sizeof(iotxn_proto_data_t), "sdmmc protocol data too large\n");

#define SDMMC_SIGNAL_VOLTAGE_330   0
#define SDMMC_SIGNAL_VOLTAGE_180   1

#define IOCTL_SDMMC_SET_SIGNAL_VOLTAGE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 0)

#define SDMMC_BUS_WIDTH_1   0
#define SDMMC_BUS_WIDTH_4   1
#define SDMMC_BUS_WIDTH_8   2

#define IOCTL_SDMMC_SET_BUS_WIDTH \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 1)

#define IOCTL_SDMMC_SET_BUS_FREQ \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 2)

#define SDMMC_TIMING_LEGACY 0
#define SDMMC_TIMING_HS     1
#define SDMMC_TIMING_HS200  2
#define SDMMC_TIMING_HS400  3

#define IOCTL_SDMMC_SET_TIMING \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 3)

#define IOCTL_SDMMC_HW_RESET \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 4)

__END_CDECLS;
