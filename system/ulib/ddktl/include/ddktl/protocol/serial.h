// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/serial.fidl INSTEAD.

#pragma once

#include <ddk/protocol/serial.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "serial-internal.h"

// DDK serial-protocol support
//
// :: Proxies ::
//
// ddk::SerialProtocolProxy is a simple wrapper around
// serial_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::SerialProtocol is a mixin class that simplifies writing DDK drivers
// that implement the serial protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SERIAL device.
// class SerialDevice {
// using SerialDeviceType = ddk::Device<SerialDevice, /* ddk mixins */>;
//
// class SerialDevice : public SerialDeviceType,
//                      public ddk::SerialProtocol<SerialDevice> {
//   public:
//     SerialDevice(zx_device_t* parent)
//         : SerialDeviceType("my-serial-protocol-device", parent) {}
//
//     zx_status_t SerialGetInfo(serial_port_info_t* out_info);
//
//     zx_status_t SerialConfig(uint32_t baud_rate, uint32_t flags);
//
//     zx_status_t SerialOpenSocket(zx_handle_t* out_handle);
//
//     ...
// };

namespace ddk {

// High level serial protocol for use by client drivers.
// When used with the platform device protocol, "port" will be relative to
// the list of serial ports assigned to your device rather than the global
// list of serial ports.
template <typename D>
class SerialProtocol : public internal::base_protocol {
public:
    SerialProtocol() {
        internal::CheckSerialProtocolSubclass<D>();
        ops_.get_info = SerialGetInfo;
        ops_.config = SerialConfig;
        ops_.open_socket = SerialOpenSocket;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_SERIAL;
        ddk_proto_ops_ = &ops_;
    }

protected:
    serial_protocol_ops_t ops_ = {};

private:
    static zx_status_t SerialGetInfo(void* ctx, serial_port_info_t* out_info) {
        return static_cast<D*>(ctx)->SerialGetInfo(out_info);
    }
    // Configures the given serial port.
    static zx_status_t SerialConfig(void* ctx, uint32_t baud_rate, uint32_t flags) {
        return static_cast<D*>(ctx)->SerialConfig(baud_rate, flags);
    }
    // Returns a socket that can be used for reading and writing data
    // from the given serial port.
    static zx_status_t SerialOpenSocket(void* ctx, zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->SerialOpenSocket(out_handle);
    }
};

class SerialProtocolProxy {
public:
    SerialProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    SerialProtocolProxy(const serial_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(serial_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    zx_status_t GetInfo(serial_port_info_t* out_info) { return ops_->get_info(ctx_, out_info); }
    // Configures the given serial port.
    zx_status_t Config(uint32_t baud_rate, uint32_t flags) {
        return ops_->config(ctx_, baud_rate, flags);
    }
    // Returns a socket that can be used for reading and writing data
    // from the given serial port.
    zx_status_t OpenSocket(zx_handle_t* out_handle) { return ops_->open_socket(ctx_, out_handle); }

private:
    serial_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
