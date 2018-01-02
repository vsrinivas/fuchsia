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
typedef void (*i2c_complete_cb)(zx_status_t status, const uint8_t* data, size_t actual,
                                void* cookie);

// Protocol for an i2c channel
typedef struct {
    zx_status_t (*transact)(void* ctx, const void* write_buf, size_t write_length,
                            size_t read_length, i2c_complete_cb complete_cb, void* cookie);
    zx_status_t (*set_bitrate)(void* ctx, uint32_t bitrate);
    zx_status_t (*get_max_transfer_size)(void* ctx, size_t* out_size);
    void (*channel_release)(void* ctx);
} i2c_channel_ops_t;

typedef struct {
    i2c_channel_ops_t* ops;
    void* ctx;
} i2c_channel_t;

// Writes and reads data on an i2c channel. If both write_length and read_length
// are greater than zero, this call will perform a write operation immediately followed
// by a read operation with no other traffic occuring on the bus in between.
// If read_length is zero, then i2c_transact will only perform a write operation,
// and if write_length is zero, then it will only perform a read operation.
// The results of the operation are returned asynchronously via the complete_cb.
// The cookie parameter can be used to pass your own private data to the complete_cb callback.
static inline zx_status_t i2c_transact(i2c_channel_t* channel, const void* write_buf,
                                       size_t write_length, size_t read_length,
                                       i2c_complete_cb complete_cb, void* cookie) {
    return channel->ops->transact(channel->ctx, write_buf, write_length, read_length, complete_cb,
                                  cookie);
}

// Sets the bitrate for the i2c channel
static inline zx_status_t i2c_set_bitrate(i2c_channel_t* channel, uint32_t bitrate) {
    return channel->ops->set_bitrate(channel->ctx, bitrate);
}

// releases any resources owned by the i2c channel
static inline void i2c_channel_release(i2c_channel_t* channel) {
    channel->ops->channel_release(channel->ctx);
}

// Returns the maximum transfer size for read and write operations on the channel.
static inline zx_status_t i2c_get_max_transfer_size(i2c_channel_t* channel, size_t* out_size) {
    return channel->ops->get_max_transfer_size(channel->ctx, out_size);
}

// Protocol for i2c
typedef struct {
    zx_status_t (*get_channel)(void* ctx, uint32_t channel_id, i2c_channel_t* channel);
    zx_status_t (*get_channel_by_address)(void* ctx, uint32_t bus_id, uint16_t address,
                                          i2c_channel_t* channel);
} i2c_protocol_ops_t;

typedef struct {
    i2c_protocol_ops_t* ops;
    void* ctx;
} i2c_protocol_t;

// Returns an i2c channel protocol based on an abstract channel ID.
// Intended for generic drivers that do not know the details
//  of the platform they are running on.
static inline zx_status_t i2c_get_channel(i2c_protocol_t* i2c, uint32_t channel_id,
                                          i2c_channel_t* channel) {
    return i2c->ops->get_channel(i2c->ctx, channel_id, channel);
}

// Returns an i2c channel protocol based on a bus ID and address.
// Addresses with the high 4 bits set (I2C_10_BIT_ADDR_MASK) are treated as 10-bit addresses.
// Otherwise the address is treated as 7-bit.
// This is intended for platform-specific drivers that know the details
// of the platform they are running on.
static inline zx_status_t i2c_get_channel_by_address(i2c_protocol_t* i2c, uint32_t bus_id,
                                                     uint16_t address, i2c_channel_t* channel) {
    return i2c->ops->get_channel_by_address(i2c->ctx, bus_id, address, channel);
}

// Helper for synchronous i2c transactions
typedef struct {
    completion_t completion;
    void* read_buf;
    size_t actual;
    zx_status_t result;
} pdev_i2c_ctx_t;

static inline void pdev_i2c_sync_cb(zx_status_t status, const uint8_t* data, size_t actual,
                                    void* cookie) {
    pdev_i2c_ctx_t* ctx = (pdev_i2c_ctx_t *)cookie;
    ctx->result = status;
    ctx->actual = actual;
    if (status == ZX_OK && ctx->read_buf && actual) {
        memcpy(ctx->read_buf, data, actual);
    }

    completion_signal(&ctx->completion);
}

static inline zx_status_t i2c_transact_sync(i2c_channel_t* channel, const void* write_buf,
                                            size_t write_length, void* read_buf, size_t read_length,
                                            size_t* out_read_actual) {
    pdev_i2c_ctx_t ctx;
    completion_reset(&ctx.completion);
    ctx.read_buf = read_buf;

    zx_status_t status = i2c_transact(channel, write_buf, write_length, read_length,
                                       pdev_i2c_sync_cb, &ctx);
    if (status != ZX_OK) {
        return status;
    }
    status = completion_wait(&ctx.completion, ZX_TIME_INFINITE);
    if (status == ZX_OK) {
        if (out_read_actual) {
            *out_read_actual = ctx.actual;
        }
        return ctx.result;
    } else {
        return status;
    }
}

__END_CDECLS;
