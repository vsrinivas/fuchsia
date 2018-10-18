// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/i2c.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct i2c_op i2c_op_t;
typedef struct i2c_protocol i2c_protocol_t;
typedef void (*i2c_transact_callback)(void* ctx, zx_status_t status, const i2c_op_t* op_list,
                                      size_t op_count);

// Declarations

#define I2C_10_BIT_ADDR_MASK UINT32_C(0xF000)

#define I2C_MAX_RW_OPS UINT32_C(8)

// See `Transact` below for usage.
struct i2c_op {
    void* data_buffer;
    size_t data_size;
    bool is_read;
    bool stop;
};

typedef struct i2c_protocol_ops {
    void (*transact)(void* ctx, const i2c_op_t* op_list, size_t op_count,
                     i2c_transact_callback callback, void* cookie);
    zx_status_t (*get_max_transfer_size)(void* ctx, size_t* out_size);
    zx_status_t (*get_interrupt)(void* ctx, uint32_t flags, zx_handle_t* out_irq);
} i2c_protocol_ops_t;

struct i2c_protocol {
    i2c_protocol_ops_t* ops;
    void* ctx;
};

// Writes and reads data on an i2c channel. Up to I2C_MAX_RW_OPS operations can be passed in.
// For write ops, i2c_op_t.data points to data to write.  The data to write does not need to be
// kept alive after this call.  For read ops, i2c_op_t.data is ignored.  Any combination of reads
// and writes can be specified.  At least the last op must have the stop flag set.
// The results of the operations are returned asynchronously via the transact_cb.
// The cookie parameter can be used to pass your own private data to the transact_cb callback.
static inline void i2c_transact(const i2c_protocol_t* proto, const i2c_op_t* op_list,
                                size_t op_count, i2c_transact_callback callback, void* cookie) {
    proto->ops->transact(proto->ctx, op_list, op_count, callback, cookie);
}
// Returns the maximum transfer size for read and write operations on the channel.
static inline zx_status_t i2c_get_max_transfer_size(const i2c_protocol_t* proto, size_t* out_size) {
    return proto->ops->get_max_transfer_size(proto->ctx, out_size);
}
static inline zx_status_t i2c_get_interrupt(const i2c_protocol_t* proto, uint32_t flags,
                                            zx_handle_t* out_irq) {
    return proto->ops->get_interrupt(proto->ctx, flags, out_irq);
}

__END_CDECLS;
