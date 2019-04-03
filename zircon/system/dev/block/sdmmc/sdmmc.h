// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/sdmmc.h>
#include <ddk/protocol/sdio.h>
#include <ddk/trace/event.h>
#include <hw/sdmmc.h>

#include <stdbool.h>

#include <threads.h>

__BEGIN_CDECLS

typedef enum sdmmc_type {
    SDMMC_TYPE_UNKNOWN,
    SDMMC_TYPE_SD,
    SDMMC_TYPE_MMC,
    SDMMC_TYPE_SDIO,
} sdmmc_type_t;

#define SDMMC_REQ_COUNT   16

// If enabled, gather stats on concurrent io ops,
// pending txns, etc. Print them whenever the block
// info is queried (lsblk will provoke this)
#define WITH_STATS 1

// SDIO cards support one common function and up to seven I/O functions. This struct is used to keep
// track of each function's state as they can be configured independently.
typedef struct sdio_function {
    sdio_func_hw_info_t hw_info;
    uint16_t cur_blk_size;
    bool enabled;
    bool intr_enabled;
} sdio_function_t;

typedef struct sdio_device {
    sdio_device_hw_info_t hw_info;
    sdio_function_t funcs[SDIO_MAX_FUNCS];
} sdio_device_t;

typedef struct sdmmc_device {
    trace_async_id_t async_id;

    zx_device_t* zxdev;
    zx_device_t* child_zxdev;

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
    sdio_device_t sdio_dev;

    mtx_t lock;

    // blockio requests
    list_node_t txn_list;

    // outstanding request (1 right now)
    sdmmc_req_t req;

    thrd_t worker_thread;
    zx_handle_t worker_event;

    //Requires lock to be acquired.
    //TODO(ravoorir): When converting to c++ use
    //clang thread annotations.
    bool worker_thread_started;
    bool dead;

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
    return (dev->host_info.caps & (SDMMC_HOST_CAP_ADMA2 | SDMMC_HOST_CAP_SIXTY_FOUR_BIT));
}

// SD/MMC shared ops

zx_status_t sdmmc_go_idle(sdmmc_device_t* dev);
zx_status_t sdmmc_send_status(sdmmc_device_t* dev, uint32_t* response);
zx_status_t sdmmc_stop_transmission(sdmmc_device_t* dev);

// SD ops

zx_status_t sd_send_op_cond(sdmmc_device_t* dev, uint32_t flags, uint32_t* ocr);
zx_status_t sd_send_if_cond(sdmmc_device_t* dev);
zx_status_t sd_select_card(sdmmc_device_t* dev);
zx_status_t sd_send_scr(sdmmc_device_t* dev, uint8_t scr[8]);
zx_status_t sd_set_bus_width(sdmmc_device_t* dev, sdmmc_bus_width_t width);

// SD/SDIO shared ops
zx_status_t sd_switch_uhs_voltage(sdmmc_device_t *dev, uint32_t ocr);
zx_status_t sd_send_relative_addr(sdmmc_device_t* dev, uint16_t* rca, uint16_t* card_status);

// SDIO ops
zx_status_t sdio_send_op_cond(sdmmc_device_t* dev, uint32_t ocr, uint32_t* rocr);
zx_status_t sdio_io_rw_direct(sdmmc_device_t* dev, bool write, uint32_t fn_idx,
                              uint32_t reg_addr, uint8_t write_byte, uint8_t *read_byte);
zx_status_t sdio_io_rw_extended(sdmmc_device_t *dev, bool write, uint32_t fn_idx,
                                uint32_t reg_addr, bool incr, uint32_t blk_count,
                                uint32_t blk_size,  bool use_dma, uint8_t *buf,
                                zx_handle_t dma_vmo, uint64_t buf_offset);

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

__END_CDECLS
