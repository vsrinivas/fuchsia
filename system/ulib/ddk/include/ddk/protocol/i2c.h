// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <sync/completion.h>

#include <string.h>

__BEGIN_CDECLS;

#define I2C_10_BIT_ADDR_MASK 0xF000

// Completion callback for i2c_transact()
typedef void (*i2c_complete_cb)(zx_status_t status, const uint8_t* data, void* cookie);

// Protocol for i2c
typedef struct {
    zx_status_t (*transact)(void* ctx, uint32_t index, const void* write_buf, size_t write_length,
                            size_t read_length, i2c_complete_cb complete_cb, void* cookie);
    zx_status_t (*get_max_transfer_size)(void* ctx, uint32_t index, size_t* out_size);
} i2c_protocol_ops_t;

typedef struct {
    i2c_protocol_ops_t* ops;
    void* ctx;
} i2c_protocol_t;

// Writes and reads data on an i2c channel. If both write_length and read_length
// are greater than zero, this call will perform a write operation immediately followed
// by a read operation with no other traffic occuring on the bus in between.
// If read_length is zero, then i2c_transact will only perform a write operation,
// and if write_length is zero, then it will only perform a read operation.
// The results of the operation are returned asynchronously via the complete_cb.
// The cookie parameter can be used to pass your own private data to the complete_cb callback.
static inline zx_status_t i2c_transact(i2c_protocol_t* i2c, uint32_t index, const void* write_buf,
                                       size_t write_length, size_t read_length,
                                       i2c_complete_cb complete_cb, void* cookie) {
    return i2c->ops->transact(i2c->ctx, index, write_buf, write_length, read_length, complete_cb,
                                  cookie);
}

// Returns the maximum transfer size for read and write operations on the channel.
static inline zx_status_t i2c_get_max_transfer_size(i2c_protocol_t* i2c, uint32_t index, size_t* out_size) {
    return i2c->ops->get_max_transfer_size(i2c->ctx, index, out_size);
}

// Helper for synchronous i2c transactions
typedef struct {
    completion_t completion;
    void* read_buf;
    size_t read_length;
    zx_status_t result;
} pdev_i2c_ctx_t;

static inline void pdev_i2c_sync_cb(zx_status_t status, const uint8_t* data, void* cookie) {
    pdev_i2c_ctx_t* ctx = (pdev_i2c_ctx_t *)cookie;
    ctx->result = status;
    if (status == ZX_OK && ctx->read_buf && ctx->read_length) {
        memcpy(ctx->read_buf, data, ctx->read_length);
    }

    completion_signal(&ctx->completion);
}

static inline zx_status_t i2c_transact_sync(i2c_protocol_t* i2c, uint32_t index,
                                            const void* write_buf, size_t write_length,
                                            void* read_buf, size_t read_length) {
    pdev_i2c_ctx_t ctx;
    completion_reset(&ctx.completion);
    ctx.read_buf = read_buf;
    ctx.read_length = read_length;

    zx_status_t status = i2c_transact(i2c, index, write_buf, write_length, read_length,
                                       pdev_i2c_sync_cb, &ctx);
    if (status != ZX_OK) {
        return status;
    }
    status = completion_wait(&ctx.completion, ZX_TIME_INFINITE);
    if (status == ZX_OK) {
        return ctx.result;
    } else {
        return status;
    }
}

// Low-level protocol for i2c drivers
typedef void (*i2c_impl_complete_cb)(zx_status_t status, void* cookie);

typedef struct {
    uint32_t (*get_bus_count)(void* ctx);
    zx_status_t (*get_max_transfer_size)(void* ctx, uint32_t bus_id, size_t* out_size);
    zx_status_t (*set_bitrate)(void* ctx, uint32_t bus_id, uint32_t bitrate);
    zx_status_t (*transact)(void* ctx, uint32_t bus_id, uint16_t address, const void* write_buf,
                            size_t write_length, void* read_buf, size_t read_length);
} i2c_impl_ops_t;

typedef struct {
    i2c_impl_ops_t* ops;
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
