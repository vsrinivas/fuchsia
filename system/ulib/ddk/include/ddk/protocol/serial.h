// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/serial.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct serial_port_info serial_port_info_t;
typedef struct serial_protocol serial_protocol_t;

// Declarations

struct serial_port_info {
    uint32_t serial_class;
    // Vendor and product ID of hardware attached to this serial port,
    // or zero if not applicable.
    uint32_t serial_vid;
    uint32_t serial_pid;
};

typedef struct serial_protocol_ops {
    zx_status_t (*get_info)(void* ctx, serial_port_info_t* out_info);
    zx_status_t (*config)(void* ctx, uint32_t baud_rate, uint32_t flags);
    zx_status_t (*open_socket)(void* ctx, zx_handle_t* out_handle);
} serial_protocol_ops_t;

// High level serial protocol for use by client drivers.
// When used with the platform device protocol, "port" will be relative to
// the list of serial ports assigned to your device rather than the global
// list of serial ports.
struct serial_protocol {
    serial_protocol_ops_t* ops;
    void* ctx;
};

static inline zx_status_t serial_get_info(const serial_protocol_t* proto,
                                          serial_port_info_t* out_info) {
    return proto->ops->get_info(proto->ctx, out_info);
}
// Configures the given serial port.
static inline zx_status_t serial_config(const serial_protocol_t* proto, uint32_t baud_rate,
                                        uint32_t flags) {
    return proto->ops->config(proto->ctx, baud_rate, flags);
}
// Returns a socket that can be used for reading and writing data
// from the given serial port.
static inline zx_status_t serial_open_socket(const serial_protocol_t* proto,
                                             zx_handle_t* out_handle) {
    return proto->ops->open_socket(proto->ctx, out_handle);
}

__END_CDECLS;
