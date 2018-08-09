// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Low-level protocol for i2c drivers
typedef void (*i2c_impl_complete_cb)(zx_status_t status, void* cookie);

typedef struct {
    uint32_t (*get_bus_count)(void* ctx);
    zx_status_t (*get_max_transfer_size)(void* ctx, uint32_t bus_id, size_t* out_size);
    zx_status_t (*set_bitrate)(void* ctx, uint32_t bus_id, uint32_t bitrate);
    zx_status_t (*transact)(void* ctx, uint32_t bus_id, uint16_t address, const void* write_buf,
                            size_t write_length, void* read_buf, size_t read_length);
} i2c_impl_protocol_ops_t;

typedef struct {
    i2c_impl_protocol_ops_t* ops;
    void* ctx;
} i2c_impl_protocol_t;

static inline uint32_t i2c_impl_get_bus_count(i2c_impl_protocol_t* i2c) {
    return i2c->ops->get_bus_count(i2c->ctx);
}

static inline zx_status_t i2c_impl_get_max_transfer_size(i2c_impl_protocol_t* i2c,
                                                         uint32_t bus_id, size_t* out_size) {
    return i2c->ops->get_max_transfer_size(i2c->ctx, bus_id, out_size);
}

// Sets the bitrate for the i2c bus in KHz units
static inline zx_status_t i2c_impl_set_bitrate(i2c_impl_protocol_t* i2c, uint32_t bus_id,
                                               uint32_t bitrate) {
    return i2c->ops->set_bitrate(i2c->ctx, bus_id, bitrate);
}

static inline zx_status_t i2c_impl_transact(i2c_impl_protocol_t* i2c, uint32_t bus_id,
                                            uint16_t address, const void* write_buf,
                                            size_t write_length, void* read_buf,
                                            size_t read_length) {
    return i2c->ops->transact(i2c->ctx, bus_id, address, write_buf, write_length, read_buf,
                              read_length);
}

__END_CDECLS;
