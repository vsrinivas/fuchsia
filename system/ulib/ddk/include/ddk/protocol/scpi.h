// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#define MAX_DVFS_OPPS       16

__BEGIN_CDECLS;

typedef struct {
    uint32_t freq_hz;
    uint32_t volt_mv;
} __PACKED scpi_opp_entry_t;

typedef struct {
    scpi_opp_entry_t opp[MAX_DVFS_OPPS];
    uint32_t latency; /* in usecs */
    uint32_t count;
} __PACKED scpi_opp_t;

typedef struct {
    zx_status_t (*get_sensor)(void* ctx, const char* name, uint32_t* sensor_value);
    zx_status_t (*get_sensor_value)(void* ctx, uint32_t sensor_id, uint32_t* sensor_value);
    zx_status_t (*get_dvfs_info)(void* ctx, uint8_t power_domain, scpi_opp_t* opps);
    zx_status_t (*get_dvfs_idx)(void* ctx, uint8_t power_domain, uint16_t* idx);
    zx_status_t (*set_dvfs_idx)(void* ctx, uint8_t power_domain, uint16_t idx);
} scpi_protocol_ops_t;

typedef struct {
    scpi_protocol_ops_t* ops;
    void* ctx;
} scpi_protocol_t;

// Get the sensor id
static inline zx_status_t scpi_get_sensor(scpi_protocol_t* scpi, const char* name,
                                          uint32_t* sensor_value) {
    return scpi->ops->get_sensor(scpi->ctx, name, sensor_value);
}

// Get the sensor id's value
static inline zx_status_t scpi_get_sensor_value(scpi_protocol_t* scpi, uint32_t sensor_id,
                                                uint32_t* sensor_value) {
    return scpi->ops->get_sensor_value(scpi->ctx, sensor_id, sensor_value);
}

// Get the DVFS info
static inline zx_status_t scpi_get_dvfs_info(scpi_protocol_t* scpi, uint8_t power_domain,
                                             scpi_opp_t* opps) {
    return scpi->ops->get_dvfs_info(scpi->ctx, power_domain, opps);
}

// Get the current operating point from DVFS table
static inline zx_status_t scpi_get_dvfs_idx(scpi_protocol_t* scpi, uint8_t power_domain,
                                            uint16_t* idx) {
    return scpi->ops->get_dvfs_idx(scpi->ctx, power_domain, idx);
}

// Set a new operating point from the DVFS table
static inline zx_status_t scpi_set_dvfs_idx(scpi_protocol_t* scpi, uint8_t power_domain,
                                            uint16_t idx) {
    return scpi->ops->set_dvfs_idx(scpi->ctx, power_domain, idx);
}
__END_CDECLS;
