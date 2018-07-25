// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/listnode.h>

#include <ddk/protocol/block.h>

__BEGIN_CDECLS;

typedef enum sdmmc_voltage {
    SDMMC_VOLTAGE_330,
    SDMMC_VOLTAGE_180,
    SDMMC_VOLTAGE_MAX,
} sdmmc_voltage_t;

typedef enum sdmmc_bus_width {
    SDMMC_BUS_WIDTH_1,
    SDMMC_BUS_WIDTH_4,
    SDMMC_BUS_WIDTH_8,
    SDMMC_BUS_WIDTH_MAX,
} sdmmc_bus_width_t;

typedef enum sdmmc_timing {
    SDMMC_TIMING_LEGACY,
    SDMMC_TIMING_HS,
    SDMMC_TIMING_HSDDR,
    SDMMC_TIMING_HS200,
    SDMMC_TIMING_HS400,
    SDMMC_TIMING_SDR12,
    SDMMC_TIMING_SDR25,
    SDMMC_TIMING_SDR50,
    SDMMC_TIMING_SDR104,
    SDMMC_TIMING_DDR50,
    SDMMC_TIMING_MAX,
} sdmmc_timing_t;

// block io transactions. one per client request
typedef struct sdmmc_txn {
    block_op_t bop;
    list_node_t node;
} sdmmc_txn_t;

typedef struct sdmmc_req sdmmc_req_t;

// number of pages per request - 2M per request
// matches DMA_DESC_COUNT in dev/block/sdhci
#define SDMMC_PAGES_COUNT  (PAGE_SIZE / sizeof(zx_paddr_t))

// sdmmc requests. one per command
struct sdmmc_req {
    uint32_t cmd_idx;
    uint32_t cmd_flags;
    uint32_t arg;

    // data command parameters
    uint16_t blockcount;
    uint16_t blocksize;
    bool use_dma;
    zx_handle_t dma_vmo; // Used if use_dma is true
    void* virt;          // Used if use_dma is false
    uint64_t buf_offset; // offset into dma_vmo or virt
    zx_handle_t pmt;

    // response data
    uint32_t response[4];

    // status
    zx_status_t status;
};

typedef struct sdmmc_host_info {
    // Controller capabilities
    uint64_t caps;
#define SDMMC_HOST_CAP_BUS_WIDTH_8   (1 << 0)
#define SDMMC_HOST_CAP_ADMA2         (1 << 1)
#define SDMMC_HOST_CAP_64BIT         (1 << 2)
#define SDMMC_HOST_CAP_VOLTAGE_330   (1 << 3)
#define SDMMC_HOST_CAP_AUTO_CMD12    (1 << 4)
    // Maximum data request size
    uint64_t max_transfer_size;

    // Host specific preferences
    uint64_t prefs;
#define SDMMC_HOST_PREFS_DISABLE_HS400         (1 << 0)
#define SDMMC_HOST_PREFS_DISABLE_HS200         (1 << 1)

} sdmmc_host_info_t;

typedef struct sdmmc_protocol_ops {
    // get host info
    zx_status_t (*host_info)(void* ctx, sdmmc_host_info_t* info);
    // set signal voltage
    zx_status_t (*set_signal_voltage)(void* ctx, sdmmc_voltage_t voltage);
    // set bus width
    zx_status_t (*set_bus_width)(void* ctx, sdmmc_bus_width_t bus_width);
    // set bus frequency
    zx_status_t (*set_bus_freq)(void* ctx, uint32_t bus_freq);
    // set mmc timing
    zx_status_t (*set_timing)(void* ctx, sdmmc_timing_t timing);
    // issue a hw reset
    void (*hw_reset)(void* ctx);
    // perform tuning
    zx_status_t (*perform_tuning)(void* ctx);
    // issue a request
    zx_status_t (*request)(void* ctx, sdmmc_req_t* req);
    // get out-of-bandwidth irq handle for SDIO
    zx_status_t (*get_sdio_oob_irq)(void* ctx, zx_handle_t *oob_irq);
} sdmmc_protocol_ops_t;

typedef struct sdmmc_protocol {
    sdmmc_protocol_ops_t* ops;
    void* ctx;
} sdmmc_protocol_t;

static inline zx_status_t sdmmc_host_info(sdmmc_protocol_t* sdmmc, sdmmc_host_info_t* info) {
    return sdmmc->ops->host_info(sdmmc->ctx, info);
}

static inline zx_status_t sdmmc_get_sdio_oob_irq(sdmmc_protocol_t* sdmmc,
                                                 zx_handle_t *oob_irq_handle) {
    return sdmmc->ops->get_sdio_oob_irq(sdmmc->ctx, oob_irq_handle);
}

static inline zx_status_t sdmmc_set_signal_voltage(sdmmc_protocol_t* sdmmc,
                                                   sdmmc_voltage_t voltage) {
    return sdmmc->ops->set_signal_voltage(sdmmc->ctx, voltage);
}

static inline zx_status_t sdmmc_set_bus_width(sdmmc_protocol_t* sdmmc,
                                              sdmmc_bus_width_t bus_width) {
    return sdmmc->ops->set_bus_width(sdmmc->ctx, bus_width);
}

static inline zx_status_t sdmmc_set_bus_freq(sdmmc_protocol_t* sdmmc, uint32_t bus_freq) {
    return sdmmc->ops->set_bus_freq(sdmmc->ctx, bus_freq);
}

static inline zx_status_t sdmmc_set_timing(sdmmc_protocol_t* sdmmc, sdmmc_timing_t timing) {
    return sdmmc->ops->set_timing(sdmmc->ctx, timing);
}

static inline void sdmmc_hw_reset(sdmmc_protocol_t* sdmmc) {
    sdmmc->ops->hw_reset(sdmmc->ctx);
}

static inline zx_status_t sdmmc_perform_tuning(sdmmc_protocol_t* sdmmc) {
    return sdmmc->ops->perform_tuning(sdmmc->ctx);
}

static inline zx_status_t sdmmc_request(sdmmc_protocol_t* sdmmc, sdmmc_req_t* req) {
    return sdmmc->ops->request(sdmmc->ctx, req);
}

__END_CDECLS;
