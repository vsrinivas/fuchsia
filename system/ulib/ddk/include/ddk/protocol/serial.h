// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/device/serial.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    uint32_t serial_class;
    // vendor and product ID of hardware attached to this serial port,
    // or zero if not applicable
    uint32_t serial_vid;
    uint32_t serial_pid;
} serial_port_info_t;

// High level serial protocol for use by client drivers
// When used with the platform device protocol, "port" will be relative to
// the list of serial ports assigned to your device rather than the global
// list of serial ports.
typedef struct {
    zx_status_t (*get_info)(void* ctx, serial_port_info_t* info);
    zx_status_t (*config)(void* ctx, uint32_t baud_rate, uint32_t flags);
    zx_status_t (*open_socket)(void* ctx, zx_handle_t* out_handle);
} serial_protocol_ops_t;

typedef struct {
    serial_protocol_ops_t* ops;
    void* ctx;
} serial_protocol_t;

static inline zx_status_t serial_get_info(serial_protocol_t* serial, serial_port_info_t* info) {
    return serial->ops->get_info(serial->ctx, info);
}

// configures the given serial port
static inline zx_status_t serial_config(serial_protocol_t* serial, uint32_t baud_rate,
                                        uint32_t flags) {
    return serial->ops->config(serial->ctx, baud_rate, flags);
}

// returns a socket that can be used for reading and writing data
// from the given serial port
static inline zx_status_t serial_open_socket(serial_protocol_t* serial, zx_handle_t* out_handle) {
    return serial->ops->open_socket(serial->ctx, out_handle);
}

__END_CDECLS;
