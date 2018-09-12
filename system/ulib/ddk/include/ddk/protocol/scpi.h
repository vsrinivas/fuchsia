// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/scpi.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct scpi_opp_entry scpi_opp_entry_t;
typedef struct scpi_opp scpi_opp_t;
typedef struct scpi_protocol scpi_protocol_t;

// Declarations

#define MAX_DVFS_OPPS UINT32_C(16)

struct scpi_opp_entry {
    uint32_t freq_hz;
    uint32_t volt_mv;
};

struct scpi_opp {
    scpi_opp_entry_t opp[16];
    // In usecs.
    uint32_t latency;
    uint32_t count;
};

typedef struct scpi_protocol_ops {
    zx_status_t (*get_sensor)(void* ctx, const char* name, uint32_t* out_sensor_id);
    zx_status_t (*get_sensor_value)(void* ctx, uint32_t sensor_id, uint32_t* out_sensor_value);
    zx_status_t (*get_dvfs_info)(void* ctx, uint8_t power_domain, scpi_opp_t* out_opps);
    zx_status_t (*get_dvfs_idx)(void* ctx, uint8_t power_domain, uint16_t* out_index);
    zx_status_t (*set_dvfs_idx)(void* ctx, uint8_t power_domain, uint16_t index);
} scpi_protocol_ops_t;

struct scpi_protocol {
    scpi_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t scpi_get_sensor(const scpi_protocol_t* proto, const char* name,
                                          uint32_t* out_sensor_id) {
    return proto->ops->get_sensor(proto->ctx, name, out_sensor_id);
}
static inline zx_status_t scpi_get_sensor_value(const scpi_protocol_t* proto, uint32_t sensor_id,
                                                uint32_t* out_sensor_value) {
    return proto->ops->get_sensor_value(proto->ctx, sensor_id, out_sensor_value);
}
static inline zx_status_t scpi_get_dvfs_info(const scpi_protocol_t* proto, uint8_t power_domain,
                                             scpi_opp_t* out_opps) {
    return proto->ops->get_dvfs_info(proto->ctx, power_domain, out_opps);
}
static inline zx_status_t scpi_get_dvfs_idx(const scpi_protocol_t* proto, uint8_t power_domain,
                                            uint16_t* out_index) {
    return proto->ops->get_dvfs_idx(proto->ctx, power_domain, out_index);
}
static inline zx_status_t scpi_set_dvfs_idx(const scpi_protocol_t* proto, uint8_t power_domain,
                                            uint16_t index) {
    return proto->ops->set_dvfs_idx(proto->ctx, power_domain, index);
}

__END_CDECLS;
