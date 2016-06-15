// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#pragma once

#include <compiler.h>
#include <err.h>
#include <stdbool.h>
#include <sys/types.h>

__BEGIN_CDECLS

typedef enum {
    BWCC_DEV_SDIO,
    BWCC_DEV_SERIAL_DMA_IO,
    BWCC_DEV_I2C0,
    BWCC_DEV_I2C1,
    BWCC_DEV_SPI0,
    BWCC_DEV_SPI1,
    BWCC_DEV_UART0,
    BWCC_DEV_UART1,
    BWCC_DEV_SST,
} bwcc_device_id_t;

status_t bwcc_hide_device(bwcc_device_id_t which, bool hide);
status_t bwcc_disable_device(bwcc_device_id_t which, bool disable);

__END_CDECLS
