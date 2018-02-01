// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// flags for serial_config()
enum {
    SERIAL_DATA_BITS_5 = (0 << 0),
    SERIAL_DATA_BITS_6 = (1 << 0),
    SERIAL_DATA_BITS_7 = (2 << 0),
    SERIAL_DATA_BITS_8 = (3 << 0),
    SERIAL_DATA_BITS_MASK = (3 << 0),

    SERIAL_STOP_BITS_1 = (0 << 2),
    SERIAL_STOP_BITS_2 = (1 << 2),
    SERIAL_STOP_BITS_MASK = (1 << 2),

    SERIAL_PARITY_NONE  = (0 << 3),
    SERIAL_PARITY_EVEN  = (1 << 3),
    SERIAL_PARITY_ODD  = (2 << 3),
    SERIAL_PARITY_MASK  = (3 << 3),

    SERIAL_FLOW_CTRL_NONE = (0 << 5),
    SERIAL_FLOW_CTRL_CTS_RTS = (1 << 5),
    SERIAL_FLOW_CTRL_MASK = (1 << 5),

    // Set this flag to change baud rate but leave other properties unchanged
    SERIAL_SET_BAUD_RATE_ONLY = (1 << 31),
};

// High level serial protocol for use by client drivers
// When used with the platform device protocol, "port" will be relative to
// the list of serial ports assigned to your device rather than the global
// list of serial ports.
typedef struct {
    zx_status_t (*config)(void* ctx, uint32_t port_num, uint32_t baud_rate, uint32_t flags);
    zx_status_t (*open_socket)(void* ctx, uint32_t port_num, zx_handle_t* out_handle);
} serial_protocol_ops_t;

typedef struct {
    serial_protocol_ops_t* ops;
    void* ctx;
} serial_protocol_t;

// configures the given serial port
static inline zx_status_t serial_config(serial_protocol_t* serial, uint32_t port_num,
                                        uint32_t baud_rate, uint32_t flags) {
    return serial->ops->config(serial->ctx, port_num, baud_rate, flags);
}

// returns a socket that can be used for reading and writing data
// from the given serial port
static inline zx_status_t serial_open_socket(serial_protocol_t* serial, uint32_t port_num,
                                             zx_handle_t* out_handle) {
    return serial->ops->open_socket(serial->ctx, port_num, out_handle);
}

// Low level serial protocol to be implemented by serial drivers
// This is only used by bus drivers like platform bus

// state flags for serial_notify_cb
enum {
    SERIAL_STATE_READABLE = (1 << 0),
    SERIAL_STATE_WRITABLE = (1 << 1),
};

// Callback for notification of readable/writeable state changes
// This may be called from an interrupt thread it should just signal another thread
// and return as soon as possible. In particular, it may not be safe to make protocol calls
// from these callbacks.
typedef void (*serial_notify_cb)(uint32_t port_num, uint32_t state, void* cookie);

typedef struct {
    uint32_t (*get_port_count)(void* ctx);
    zx_status_t (*config)(void* ctx, uint32_t port_num, uint32_t baud_rate, uint32_t flags);
    zx_status_t (*enable)(void* ctx, uint32_t port_num, bool enable);
    zx_status_t (*read)(void* ctx, uint32_t port_num, void* buf, size_t length, size_t* out_actual);
    zx_status_t (*write)(void* ctx, uint32_t port_num, const void* buf, size_t length,
                         size_t* out_actual);
    zx_status_t (*set_notify_callback)(void* ctx, uint32_t port_num, serial_notify_cb cb,
                                       void* cookie);
} serial_driver_ops_t;

typedef struct {
    serial_driver_ops_t* ops;
    void* ctx;
} serial_driver_protocol_t;

static inline uint32_t serial_driver_get_port_count(serial_driver_protocol_t* serial) {
    return serial->ops->get_port_count(serial->ctx);
}

// Configures the given serial port
static inline zx_status_t serial_driver_config(serial_driver_protocol_t* serial, uint32_t port_num,
                                               uint32_t baud_rate, uint32_t flags) {
    return serial->ops->config(serial->ctx, port_num, baud_rate, flags);
}

// Enables or disables the given serial port
static inline zx_status_t serial_driver_enable(serial_driver_protocol_t* serial, uint32_t port_num,
                                               bool enable) {
    return serial->ops->enable(serial->ctx, port_num, enable);
}

// Reads data from the given serial port
// Returns ZX_ERR_SHOULD_WAIT if no data is available to read
static inline zx_status_t serial_driver_read(serial_driver_protocol_t* serial, uint32_t port_num,
                                             void* buf, size_t length, size_t* out_actual) {
    return serial->ops->read(serial->ctx, port_num, buf, length, out_actual);
}

// Reads data from the given serial port
// Returns ZX_ERR_SHOULD_WAIT if transmit buffer is full and writing is not possible
static inline zx_status_t serial_driver_write(serial_driver_protocol_t* serial, uint32_t port_num,
                                              const void* buf, size_t length, size_t* out_actual) {
    return serial->ops->write(serial->ctx, port_num, buf, length, out_actual);
}

// Sets a callback to be called when the port's readable and writeble state changes
// Pass NULL to clear previously installed callback
// The callback may be called from an interrupt thread it should just signal another thread
// and return as soon as possible. In particular, it may not be safe to make protocol calls
// from the callback.
// Returns ZX_ERR_BAD_STATE called while the driver is in enabled state.
static inline zx_status_t serial_driver_set_notify_callback(serial_driver_protocol_t* serial,
                                                            uint32_t port_num, serial_notify_cb cb,
                                                            void* cookie) {
    return serial->ops->set_notify_callback(serial->ctx, port_num, cb, cookie);
}

__END_CDECLS;
