// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/serial_impl.fidl INSTEAD.

#pragma once

#include <ddk/protocol/serial-impl.h>
#include <ddk/protocol/serial.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "serial-impl-internal.h"

// DDK serial-impl-protocol support
//
// :: Proxies ::
//
// ddk::SerialImplProtocolProxy is a simple wrapper around
// serial_impl_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::SerialImplProtocol is a mixin class that simplifies writing DDK drivers
// that implement the serial-impl protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_SERIAL_IMPL device.
// class SerialImplDevice {
// using SerialImplDeviceType = ddk::Device<SerialImplDevice, /* ddk mixins */>;
//
// class SerialImplDevice : public SerialImplDeviceType,
//                          public ddk::SerialImplProtocol<SerialImplDevice> {
//   public:
//     SerialImplDevice(zx_device_t* parent)
//         : SerialImplDeviceType("my-serial-impl-protocol-device", parent) {}
//
//     zx_status_t SerialImplGetInfo(serial_port_info_t* out_info);
//
//     zx_status_t SerialImplConfig(uint32_t baud_rate, uint32_t flags);
//
//     zx_status_t SerialImplEnable(bool enable);
//
//     zx_status_t SerialImplRead(void* out_buf_buffer, size_t buf_size, size_t* out_buf_actual);
//
//     zx_status_t SerialImplWrite(const void* buf_buffer, size_t buf_size, size_t* out_actual);
//
//     zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb);
//
//     ...
// };

namespace ddk {

template <typename D>
class SerialImplProtocol : public internal::base_protocol {
public:
    SerialImplProtocol() {
        internal::CheckSerialImplProtocolSubclass<D>();
        ops_.get_info = SerialImplGetInfo;
        ops_.config = SerialImplConfig;
        ops_.enable = SerialImplEnable;
        ops_.read = SerialImplRead;
        ops_.write = SerialImplWrite;
        ops_.set_notify_callback = SerialImplSetNotifyCallback;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_SERIAL_IMPL;
        ddk_proto_ops_ = &ops_;
    }

protected:
    serial_impl_protocol_ops_t ops_ = {};

private:
    static zx_status_t SerialImplGetInfo(void* ctx, serial_port_info_t* out_info) {
        return static_cast<D*>(ctx)->SerialImplGetInfo(out_info);
    }
    // Configures the given serial port.
    static zx_status_t SerialImplConfig(void* ctx, uint32_t baud_rate, uint32_t flags) {
        return static_cast<D*>(ctx)->SerialImplConfig(baud_rate, flags);
    }
    static zx_status_t SerialImplEnable(void* ctx, bool enable) {
        return static_cast<D*>(ctx)->SerialImplEnable(enable);
    }
    static zx_status_t SerialImplRead(void* ctx, void* out_buf_buffer, size_t buf_size,
                                      size_t* out_buf_actual) {
        return static_cast<D*>(ctx)->SerialImplRead(out_buf_buffer, buf_size, out_buf_actual);
    }
    static zx_status_t SerialImplWrite(void* ctx, const void* buf_buffer, size_t buf_size,
                                       size_t* out_actual) {
        return static_cast<D*>(ctx)->SerialImplWrite(buf_buffer, buf_size, out_actual);
    }
    static zx_status_t SerialImplSetNotifyCallback(void* ctx, const serial_notify_t* cb) {
        return static_cast<D*>(ctx)->SerialImplSetNotifyCallback(cb);
    }
};

class SerialImplProtocolProxy {
public:
    SerialImplProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    SerialImplProtocolProxy(const serial_impl_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(serial_impl_protocol_t* proto) {
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
    zx_status_t Enable(bool enable) { return ops_->enable(ctx_, enable); }
    zx_status_t Read(void* out_buf_buffer, size_t buf_size, size_t* out_buf_actual) {
        return ops_->read(ctx_, out_buf_buffer, buf_size, out_buf_actual);
    }
    zx_status_t Write(const void* buf_buffer, size_t buf_size, size_t* out_actual) {
        return ops_->write(ctx_, buf_buffer, buf_size, out_actual);
    }
    zx_status_t SetNotifyCallback(const serial_notify_t* cb) {
        return ops_->set_notify_callback(ctx_, cb);
    }

private:
    serial_impl_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
