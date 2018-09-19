// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <lib/sync/completion.h>

#include <string.h>

__BEGIN_CDECLS;

#define I2C_10_BIT_ADDR_MASK 0xF000
#define I2C_MAX_RW_OPS 8

// See i2c_transact_cb and i2c_transact below for usage.
typedef struct {
    void* buf;
    uint32_t length;
    bool is_read;
    bool stop;
} i2c_op_t;

typedef struct {
    uint32_t length;
    bool is_read;
    bool stop;
} __PACKED i2c_rpc_op_t;

typedef struct {
    sync_completion_t completion;
    void* read_buf;
    size_t read_length;
    zx_status_t result;
} i2c_write_read_ctx_t;

// Completion callback, ops contains reads performed and ops[].buf points to the data.  The
// data pointed by ops[].buf is only valid until the end of the callback.
typedef void (*i2c_transact_cb)(zx_status_t status, i2c_op_t ops[], size_t cnt, void* cookie);

// Protocol for i2c
typedef struct {
    zx_status_t (*transact)(void* ctx, i2c_op_t ops[], size_t cnt,
                            i2c_transact_cb transact_cb, void* cookie);
    zx_status_t (*get_max_transfer_size)(void* ctx, size_t* out_size);
} i2c_protocol_ops_t;

typedef struct {
    i2c_protocol_ops_t* ops;
    void* ctx;
} i2c_protocol_t;

// Writes and reads data on an i2c channel. Up to I2C_MAX_RW_OPS operations can be passed in.
// For write ops, i2c_op_t.buf points to data to write.  The data to write does not need to be
// kept alive after this call.  For read ops, i2c_op_t.buf is ignored.  Any combination of reads
// and writes can be specified.  At least the last op must have the stop flag set.
// The results of the operations are returned asynchronously via the transact_cb.
// The cookie parameter can be used to pass your own private data to the transact_cb callback.
static inline zx_status_t i2c_transact(const i2c_protocol_t* i2c, i2c_op_t ops[],
                                       size_t cnt, i2c_transact_cb transact_cb, void* cookie) {
    return i2c->ops->transact(i2c->ctx, ops, cnt, transact_cb, cookie);
}

// Writes and reads data on an i2c channel. If both write_length and read_length
// are greater than zero, this call will perform a write operation immediately followed
// by a read operation with no other traffic occuring on the bus in between.
// If read_length is zero, then i2c_write_read will only perform a write operation,
// and if write_length is zero, then it will only perform a read operation.
// The results of the operation are returned asynchronously via the transact_cb.
// The cookie parameter can be used to pass your own private data to the transact_cb callback.
static inline zx_status_t i2c_write_read(const i2c_protocol_t* i2c, const void* write_buf,
                                         size_t write_length, size_t read_length,
                                         i2c_transact_cb transact_cb, void* cookie) {
    i2c_op_t ops[2];
    size_t count = 0;
    if (write_length) {
        ops[count].buf = (void*)write_buf;
        ops[count].length = (uint32_t)write_length;
        ops[count].is_read = false;
        ops[count].stop = !read_length;
        count++;
    }
    if (read_length) {
        ops[count].buf = NULL;
        ops[count].length = (uint32_t)read_length;
        ops[count].is_read = true;
        ops[count].stop = true;
        count++;
    }
    return i2c_transact(i2c, ops, count, transact_cb, cookie);
}

// Returns the maximum transfer size for read and write operations on the channel.
static inline zx_status_t i2c_get_max_transfer_size(const i2c_protocol_t* i2c, size_t* out_size) {
    return i2c->ops->get_max_transfer_size(i2c->ctx, out_size);
}

static inline void i2c_write_read_sync_cb(zx_status_t status, i2c_op_t* ops, size_t cnt,
                                          void* cookie) {
    i2c_write_read_ctx_t* ctx = (i2c_write_read_ctx_t*)cookie;
    ctx->result = status;
    if (status == ZX_OK && ctx->read_buf && ctx->read_length) {
        ZX_DEBUG_ASSERT(cnt == 1);
        memcpy(ctx->read_buf, ops[0].buf, ctx->read_length);
    }

    sync_completion_signal(&ctx->completion);
}

static inline zx_status_t i2c_write_read_sync(const i2c_protocol_t* i2c, const void* write_buf,
                                              size_t write_length, void* read_buf,
                                              size_t read_length) {
    i2c_write_read_ctx_t ctx;
    sync_completion_reset(&ctx.completion);
    ctx.read_buf = read_buf;
    ctx.read_length = read_length;

    zx_status_t status = i2c_write_read(i2c, write_buf, write_length,
                                        read_length, i2c_write_read_sync_cb, &ctx);
    if (status != ZX_OK) {
        return status;
    }
    status = sync_completion_wait(&ctx.completion, ZX_TIME_INFINITE);
    if (status == ZX_OK) {
        return ctx.result;
    } else {
        return status;
    }
}

static inline zx_status_t i2c_write_sync(const i2c_protocol_t* i2c, const void* write_buf,
                                         size_t write_length) {
    return i2c_write_read_sync(i2c, write_buf, write_length, NULL, 0);
}

static inline zx_status_t i2c_read_sync(const i2c_protocol_t* i2c, void* read_buf,
                                        size_t read_length) {
    return i2c_write_read_sync(i2c, NULL, 0, read_buf, read_length);
}

__END_CDECLS;
