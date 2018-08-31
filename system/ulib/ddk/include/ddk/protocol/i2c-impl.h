// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Low-level protocol for i2c drivers
typedef void (*i2c_impl_complete_cb)(zx_status_t status, void* cookie);

// See i2c_impl_transact below for usage.
typedef struct {
    void* buf;
    size_t length;
    bool is_read;
    bool stop;
} i2c_impl_op_t;

typedef struct {
    uint32_t (*get_bus_count)(void* ctx);
    zx_status_t (*get_max_transfer_size)(void* ctx, uint32_t bus_id, size_t* out_size);
    zx_status_t (*set_bitrate)(void* ctx, uint32_t bus_id, uint32_t bitrate);
    // transact assumes that all ops buf are not null
    // transact assumes that all ops length are not zero
    // transact assumes that at least the last op has stop set to true
    zx_status_t (*transact)(void* ctx, uint32_t bus_id, uint16_t address, i2c_impl_op_t* ops,
                            size_t count);
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

// Writes and reads data on an i2c bus.  For write ops, i2c_impl_op_t.buf points to data to write.
// For read ops, i2c_impl_op_t.buf points to the buffer where data is read into.
// Any combination of reads and writes could be specified.  At least the last op must have the stop
// flag set.  The results of the operations are returned synchronously.
static inline zx_status_t i2c_impl_transact(i2c_impl_protocol_t* i2c, uint32_t bus_id,
                                            uint16_t address, i2c_impl_op_t* ops, size_t count) {
    return i2c->ops->transact(i2c->ctx, bus_id, address, ops, count);
}

static inline zx_status_t i2c_impl_write_read(i2c_impl_protocol_t* i2c, uint32_t bus_id,
                                              uint16_t address, const void* write_buf,
                                              size_t write_length, void* read_buf,
                                              size_t read_length) {
    i2c_impl_op_t ops[2];
    size_t count = 0;
    if (write_length) {
        ops[count].buf = (void*)write_buf;
        ops[count].length = write_length;
        ops[count].is_read = false;
        ops[count].stop = !read_length;
        count++;
    }
    if (read_length) {
        ops[count].buf = read_buf;
        ops[count].length = read_length;
        ops[count].is_read = true;
        ops[count].stop = true;
        count++;
    }
    return i2c_impl_transact(i2c, bus_id, address, ops, count);
}

__END_CDECLS;
