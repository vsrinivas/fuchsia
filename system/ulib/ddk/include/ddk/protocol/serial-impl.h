// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/serial_impl.fidl INSTEAD.

#pragma once

#include <ddk/protocol/serial.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef uint32_t serial_state_t;
#define SERIAL_STATE_READABLE UINT32_C(1)
#define SERIAL_STATE_WRITABLE UINT32_C(2)

typedef struct serial_notify serial_notify_t;
typedef struct serial_impl_protocol serial_impl_protocol_t;

// Declarations

struct serial_notify {
    void (*callback)(void* ctx, serial_state_t state);
    void* ctx;
};

typedef struct serial_impl_protocol_ops {
    zx_status_t (*get_info)(void* ctx, serial_port_info_t* out_info);
    zx_status_t (*config)(void* ctx, uint32_t baud_rate, uint32_t flags);
    zx_status_t (*enable)(void* ctx, bool enable);
    zx_status_t (*read)(void* ctx, void* out_buf_buffer, size_t buf_size, size_t* out_buf_actual);
    zx_status_t (*write)(void* ctx, const void* buf_buffer, size_t buf_size, size_t* out_actual);
    zx_status_t (*set_notify_callback)(void* ctx, const serial_notify_t* cb);
} serial_impl_protocol_ops_t;

struct serial_impl_protocol {
    serial_impl_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t serial_impl_get_info(const serial_impl_protocol_t* proto,
                                               serial_port_info_t* out_info) {
    return proto->ops->get_info(proto->ctx, out_info);
}
// Configures the given serial port.
static inline zx_status_t serial_impl_config(const serial_impl_protocol_t* proto,
                                             uint32_t baud_rate, uint32_t flags) {
    return proto->ops->config(proto->ctx, baud_rate, flags);
}
static inline zx_status_t serial_impl_enable(const serial_impl_protocol_t* proto, bool enable) {
    return proto->ops->enable(proto->ctx, enable);
}
static inline zx_status_t serial_impl_read(const serial_impl_protocol_t* proto,
                                           void* out_buf_buffer, size_t buf_size,
                                           size_t* out_buf_actual) {
    return proto->ops->read(proto->ctx, out_buf_buffer, buf_size, out_buf_actual);
}
static inline zx_status_t serial_impl_write(const serial_impl_protocol_t* proto,
                                            const void* buf_buffer, size_t buf_size,
                                            size_t* out_actual) {
    return proto->ops->write(proto->ctx, buf_buffer, buf_size, out_actual);
}
static inline zx_status_t serial_impl_set_notify_callback(const serial_impl_protocol_t* proto,
                                                          const serial_notify_t* cb) {
    return proto->ops->set_notify_callback(proto->ctx, cb);
}

__END_CDECLS;
