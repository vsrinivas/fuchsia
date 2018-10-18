// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/i2c_impl.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct i2c_impl_op i2c_impl_op_t;
typedef struct i2c_impl_protocol i2c_impl_protocol_t;

// Declarations

// See `Transact` below for usage.
struct i2c_impl_op {
    uint16_t address;
    void* data_buffer;
    size_t data_size;
    bool is_read;
    bool stop;
};

typedef struct i2c_impl_protocol_ops {
    uint32_t (*get_bus_count)(void* ctx);
    zx_status_t (*get_max_transfer_size)(void* ctx, uint32_t bus_id, size_t* out_size);
    zx_status_t (*set_bitrate)(void* ctx, uint32_t bus_id, uint32_t bitrate);
    zx_status_t (*transact)(void* ctx, uint32_t bus_id, const i2c_impl_op_t* op_list,
                            size_t op_count);
} i2c_impl_protocol_ops_t;

// Low-level protocol for i2c drivers.
struct i2c_impl_protocol {
    i2c_impl_protocol_ops_t* ops;
    void* ctx;
};

static inline uint32_t i2c_impl_get_bus_count(const i2c_impl_protocol_t* proto) {
    return proto->ops->get_bus_count(proto->ctx);
}
static inline zx_status_t i2c_impl_get_max_transfer_size(const i2c_impl_protocol_t* proto,
                                                         uint32_t bus_id, size_t* out_size) {
    return proto->ops->get_max_transfer_size(proto->ctx, bus_id, out_size);
}
// Sets the bitrate for the i2c bus in KHz units.
static inline zx_status_t i2c_impl_set_bitrate(const i2c_impl_protocol_t* proto, uint32_t bus_id,
                                               uint32_t bitrate) {
    return proto->ops->set_bitrate(proto->ctx, bus_id, bitrate);
}
// |Transact| assumes that all ops buf are not null.
// |Transact| assumes that all ops length are not zero.
// |Transact| assumes that at least the last op has stop set to true.
static inline zx_status_t i2c_impl_transact(const i2c_impl_protocol_t* proto, uint32_t bus_id,
                                            const i2c_impl_op_t* op_list, size_t op_count) {
    return proto->ops->transact(proto->ctx, bus_id, op_list, op_count);
}

__END_CDECLS;
