// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/sdmmc.h>
#include <ddk/protocol/sdio.h>
#include <hw/sdmmc.h>

#include "sdio.h"
#include <threads.h>

__BEGIN_CDECLS;

typedef enum sdmmc_type {
    SDMMC_TYPE_SD,
    SDMMC_TYPE_MMC,
    SDMMC_TYPE_SDIO,
} sdmmc_type_t;

#define SDMMC_REQ_COUNT   16

// If enabled, gather stats on concurrent io ops,
// pending txns, etc. Print them whenever the block
// info is queried (lsblk will provoke this)
#define WITH_STATS 1

typedef struct sdmmc_device {
    zx_device_t* zxdev;

    sdmmc_protocol_t host;
    sdmmc_host_info_t host_info;

    sdmmc_type_t type;

    sdmmc_bus_width_t bus_width;
    sdmmc_voltage_t signal_voltage;
    sdmmc_timing_t timing;

    unsigned clock_rate;    // Bus clock rate
    uint64_t capacity;      // Card capacity

    uint16_t rca;           // Relative address

    // mmc
    uint32_t raw_cid[4];
    uint32_t raw_csd[4];
    uint8_t raw_ext_csd[512];

    // sdio
    sdio_device_info_t sdio_info;
    mtx_t lock;

    // blockio requests
    list_node_t txn_list;

    // outstanding request (1 right now)
    sdmmc_req_t req;

    thrd_t worker_thread;
    zx_handle_t worker_event;
    bool worker_thread_running;

#if WITH_STATS
    size_t stat_concur;
    size_t stat_pending;
    size_t stat_max_concur;
    size_t stat_max_pending;
    size_t stat_total_ops;
    size_t stat_total_blocks;
#endif

    block_info_t block_info;
} sdmmc_device_t;

static inline bool sdmmc_use_dma(sdmmc_device_t* dev) {
    return (dev->host_info.caps & (SDMMC_HOST_CAP_ADMA2 | SDMMC_HOST_CAP_64BIT));
}

// SD/MMC shared ops

zx_status_t sdmmc_go_idle(sdmmc_device_t* dev);
zx_status_t sdmmc_send_status(sdmmc_device_t* dev, uint32_t* response);
zx_status_t sdmmc_stop_transmission(sdmmc_device_t* dev);

// SD ops

zx_status_t sd_send_if_cond(sdmmc_device_t* dev);

// SD/SDIO shared ops
zx_status_t sd_switch_uhs_voltage(sdmmc_device_t *dev, uint32_t ocr);
zx_status_t sd_send_relative_addr(sdmmc_device_t* dev, uint16_t *rca);

// SDIO ops
zx_status_t sdio_send_op_cond(sdmmc_device_t* dev, uint32_t ocr, uint32_t* rocr);
zx_status_t sdio_io_rw_direct(sdmmc_device_t* dev, bool write, uint32_t fn_idx,
                              uint32_t reg_addr, uint8_t write_byte, uint8_t *read_byte);
zx_status_t sdio_io_rw_extended(sdmmc_device_t *dev, bool write, uint32_t fn_idx,
                                uint32_t reg_addr, bool incr, uint8_t *buf,
                                uint32_t blk_count, uint32_t blk_size);

zx_status_t sdio_enable_interrupt(void *ctx, uint8_t fn_idx);
zx_status_t sdio_disable_interrupt(void *ctx, uint8_t fn_idx);
zx_status_t sdio_enable_function(void *ctx, uint8_t fn_idx);
zx_status_t sdio_disable_function(void *ctx, uint8_t fn_idx);
zx_status_t sdio_modify_block_size(void *ctx, uint8_t fn_idx, uint16_t blk_sz, bool deflt);
zx_status_t sdio_rw_data(void *ctx, uint8_t fn_idx, sdio_rw_txn_t *txn);
zx_status_t sdio_get_oob_irq_host(void *ctx, zx_handle_t *oob_irq);


// MMC ops

zx_status_t mmc_send_op_cond(sdmmc_device_t* dev, uint32_t ocr, uint32_t* rocr);
zx_status_t mmc_all_send_cid(sdmmc_device_t* dev, uint32_t cid[4]);
zx_status_t mmc_set_relative_addr(sdmmc_device_t* dev, uint16_t rca);
zx_status_t mmc_send_csd(sdmmc_device_t* dev, uint32_t csd[4]);
zx_status_t mmc_send_ext_csd(sdmmc_device_t* dev, uint8_t ext_csd[512]);
zx_status_t mmc_select_card(sdmmc_device_t* dev);
zx_status_t mmc_switch(sdmmc_device_t* dev, uint8_t index, uint8_t value);

zx_status_t sdmmc_probe_sd(sdmmc_device_t* dev);
zx_status_t sdmmc_probe_mmc(sdmmc_device_t* dev);
zx_status_t sdmmc_probe_sdio(sdmmc_device_t* dev);

__END_CDECLS;
