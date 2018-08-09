// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/serial-impl.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "serial-impl-internal.h"

// DDK serial-impl protocol support.
//
// :: Proxies ::
//
// ddk::SerialImplProtocolProxy is a simple wrappers around serial_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::SerialImplProtocol is a mixin class that simplifies writing DDK drivers that
// implement the serial protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_NAND device.
// class SerialImplDevice;
// using SerialImplDeviceType = ddk::Device<SerialImplDevice, /* ddk mixins */>;
//
// class SerialImplDevice : public SerialDeviceType,
//                          public ddk::SerialImplProtocol<SerialImplDevice> {
//   public:
//     SerialImplDevice(zx_device_t* parent)
//       : SerialImplDeviceType("my-serial-device", parent) {}
//
//     void Query(serial_info_t* info_out, size_t* serial_op_size_out);
//     void Queue(serial_op_t* operation);
//     zx_status_t GetFactoryBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
//                                        uint32_t* num_bad_blocks);
//     ...
// };

namespace ddk {

template <typename D>
class SerialImplProtocol : public internal::base_protocol {
public:
    SerialImplProtocol() {
        internal::CheckSerialImplProtocolSubclass<D>();
        serial_proto_ops_.get_info = GetInfo;
        serial_proto_ops_.config = Config;
        serial_proto_ops_.enable = Enable;
        serial_proto_ops_.read = Read;
        serial_proto_ops_.write = Write;
        serial_proto_ops_.set_notify_callback = SetNotifyCallback;

        // Can only inherit from one base_protocol implementation.
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_SERIAL_IMPL;
        ddk_proto_ops_ = &serial_proto_ops_;
    }

protected:
    serial_impl_ops_t serial_proto_ops_ = {};

private:
    static zx_status_t GetInfo(void* ctx, serial_port_info_t* info) {
        return static_cast<D*>(ctx)->GetInfo(info);
    }

    static zx_status_t Config(void* ctx, uint32_t baud_rate, uint32_t flags) {
        return static_cast<D*>(ctx)->Config(baud_rate, flags);
    }

    static zx_status_t Enable(void* ctx, bool enable) {
        return static_cast<D*>(ctx)->Enable(enable);
    }

    static zx_status_t Read(void* ctx, void* buf, size_t length, size_t* out_actual) {
        return static_cast<D*>(ctx)->Read(buf, length, out_actual);
    }

    static zx_status_t Write(void* ctx, const void* buf, size_t length, size_t* out_actual) {
        return static_cast<D*>(ctx)->Write(buf, length, out_actual);
    }

    static zx_status_t SetNotifyCallback(void* ctx, serial_notify_cb cb, void* cookie) {
        return static_cast<D*>(ctx)->SetNotifyCallback(cb, cookie);
    }
};

class SerialImplProtocolProxy {
public:
    SerialImplProtocolProxy(serial_impl_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t GetInfo(serial_port_info_t* info) {
        return ops_->get_info(ctx_, info);
    }

    zx_status_t Config(uint32_t baud_rate, uint32_t flags) {
        return ops_->config(ctx_, baud_rate, flags);
    }

    zx_status_t Enable(bool enable) {
        return ops_->enable(ctx_, enable);
    }

    zx_status_t Read(void* buf, size_t length, size_t* out_actual) {
        return ops_->read(ctx_, buf, length, out_actual);
    }

    zx_status_t Write(const void* buf, size_t length, size_t* out_actual) {
        return ops_->write(ctx_, buf, length, out_actual);
    }

    zx_status_t SetNotifyCallback(serial_notify_cb cb, void* cookie) {
        return ops_->set_notify_callback(ctx_, cb, cookie);
    }

private:
    serial_impl_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
