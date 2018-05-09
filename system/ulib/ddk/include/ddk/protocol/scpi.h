// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;
typedef struct {
    zx_status_t (*get_sensor)(void* ctx, const char* name,
                 uint32_t* sensor_value);
    zx_status_t (*get_sensor_value)(void* ctx, uint32_t sensor_id,
                 uint32_t* sensor_value);
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
__END_CDECLS;
