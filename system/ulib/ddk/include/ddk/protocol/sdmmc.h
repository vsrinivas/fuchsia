// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/sdmmc.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef uint64_t sdmmc_host_prefs_t;
#define SDMMC_HOST_PREFS_DISABLE_HS400 UINT64_C(1)
#define SDMMC_HOST_PREFS_DISABLE_HS200 UINT64_C(2)

typedef struct sdmmc_host_info sdmmc_host_info_t;
typedef uint8_t sdmmc_bus_width_t;
#define SDMMC_BUS_WIDTH_ONE UINT8_C(0)
#define SDMMC_BUS_WIDTH_FOUR UINT8_C(1)
#define SDMMC_BUS_WIDTH_EIGHT UINT8_C(2)
#define SDMMC_BUS_WIDTH_MAX UINT8_C(3)

typedef uint8_t sdmmc_voltage_t;
#define SDMMC_VOLTAGE_V330 UINT8_C(0)
#define SDMMC_VOLTAGE_V180 UINT8_C(1)
#define SDMMC_VOLTAGE_MAX UINT8_C(2)

typedef uint64_t sdmmc_host_cap_t;
#define SDMMC_HOST_CAP_BUS_WIDTH_8 UINT64_C(1)
#define SDMMC_HOST_CAP_ADMA2 UINT64_C(2)
#define SDMMC_HOST_CAP_SIXTY_FOUR_BIT UINT64_C(4)
#define SDMMC_HOST_CAP_VOLTAGE_330 UINT64_C(8)
#define SDMMC_HOST_CAP_AUTO_CMD12 UINT64_C(16)

typedef uint8_t sdmmc_timing_t;
#define SDMMC_TIMING_LEGACY UINT8_C(0)
#define SDMMC_TIMING_HS UINT8_C(1)
#define SDMMC_TIMING_HSDDR UINT8_C(2)
#define SDMMC_TIMING_HS200 UINT8_C(3)
#define SDMMC_TIMING_HS400 UINT8_C(4)
#define SDMMC_TIMING_SDR12 UINT8_C(5)
#define SDMMC_TIMING_SDR25 UINT8_C(6)
#define SDMMC_TIMING_SDR50 UINT8_C(7)
#define SDMMC_TIMING_SDR104 UINT8_C(8)
#define SDMMC_TIMING_DDR50 UINT8_C(9)
#define SDMMC_TIMING_MAX UINT8_C(10)

typedef struct sdmmc_req sdmmc_req_t;
typedef struct sdmmc_protocol sdmmc_protocol_t;

// Declarations

// number of pages per request - 2M per request
// matches DMA_DESC_COUNT in dev/block/sdhci
// (PAGE_SIZE / sizeof(zx_paddr_t))
#define SDMMC_PAGES_COUNT UINT64_C(512)

struct sdmmc_host_info {
    // Controller capabilities
    uint64_t caps;
    // Maximum data request size
    uint64_t max_transfer_size;
    uint64_t max_transfer_size_non_dma;
    // Host specific preferences
    uint64_t prefs;
};

// sdmmc requests. one per command
struct sdmmc_req {
    uint32_t cmd_idx;
    uint32_t cmd_flags;
    uint32_t arg;
    // data command parameters
    uint16_t blockcount;
    uint16_t blocksize;
    bool use_dma;
    // Used if use_dma is true
    zx_handle_t dma_vmo;
    // Used if use_dma is false
    void* virt_buffer;
    size_t virt_size;
    // offset into dma_vmo or virt
    uint64_t buf_offset;
    zx_handle_t pmt;
    // response data
    uint32_t response[4];
    // status
    zx_status_t status;
};

typedef struct sdmmc_protocol_ops {
    zx_status_t (*host_info)(void* ctx, sdmmc_host_info_t* out_info);
    zx_status_t (*set_signal_voltage)(void* ctx, sdmmc_voltage_t voltage);
    zx_status_t (*set_bus_width)(void* ctx, sdmmc_bus_width_t bus_width);
    zx_status_t (*set_bus_freq)(void* ctx, uint32_t bus_freq);
    zx_status_t (*set_timing)(void* ctx, sdmmc_timing_t timing);
    void (*hw_reset)(void* ctx);
    zx_status_t (*perform_tuning)(void* ctx, uint32_t cmd_idx);
    zx_status_t (*request)(void* ctx, sdmmc_req_t* req);
} sdmmc_protocol_ops_t;

struct sdmmc_protocol {
    sdmmc_protocol_ops_t* ops;
    void* ctx;
};

// Get host info.
static inline zx_status_t sdmmc_host_info(const sdmmc_protocol_t* proto,
                                          sdmmc_host_info_t* out_info) {
    return proto->ops->host_info(proto->ctx, out_info);
}
// Set signal voltage.
static inline zx_status_t sdmmc_set_signal_voltage(const sdmmc_protocol_t* proto,
                                                   sdmmc_voltage_t voltage) {
    return proto->ops->set_signal_voltage(proto->ctx, voltage);
}
// Set bus width.
static inline zx_status_t sdmmc_set_bus_width(const sdmmc_protocol_t* proto,
                                              sdmmc_bus_width_t bus_width) {
    return proto->ops->set_bus_width(proto->ctx, bus_width);
}
// Set bus frequency.
static inline zx_status_t sdmmc_set_bus_freq(const sdmmc_protocol_t* proto, uint32_t bus_freq) {
    return proto->ops->set_bus_freq(proto->ctx, bus_freq);
}
// Set mmc timing.
static inline zx_status_t sdmmc_set_timing(const sdmmc_protocol_t* proto, sdmmc_timing_t timing) {
    return proto->ops->set_timing(proto->ctx, timing);
}
// Issue a hw reset.
static inline void sdmmc_hw_reset(const sdmmc_protocol_t* proto) {
    proto->ops->hw_reset(proto->ctx);
}
// Perform tuning.
static inline zx_status_t sdmmc_perform_tuning(const sdmmc_protocol_t* proto, uint32_t cmd_idx) {
    return proto->ops->perform_tuning(proto->ctx, cmd_idx);
}
// Issue a request.
static inline zx_status_t sdmmc_request(const sdmmc_protocol_t* proto, sdmmc_req_t* req) {
    return proto->ops->request(proto->ctx, req);
}

__END_CDECLS;
