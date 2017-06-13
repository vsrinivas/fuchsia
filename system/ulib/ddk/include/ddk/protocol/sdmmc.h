// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

// protocol data for iotxns
typedef struct sdmmc_protocol_data {
    uint32_t cmd;  // Command to issue to the underlying device.
    uint32_t arg;  // Argument to accompany the command.

    uint16_t blockcount;   // For IOps, number of blocks to read/write.
    uint16_t blocksize;    // For IOps, size of blocks to read/write.

    uint32_t response[4];  // Response data.
} sdmmc_protocol_data_t;

#define IOCTL_SDMMC_SET_VOLTAGE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 0)

#define IOCTL_SDMMC_SET_BUS_WIDTH \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 1)

#define IOCTL_SDMMC_SET_BUS_FREQ \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 2)

#define IOCTL_SDMMC_HW_RESET \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SDMMC, 3)

__END_CDECLS;
