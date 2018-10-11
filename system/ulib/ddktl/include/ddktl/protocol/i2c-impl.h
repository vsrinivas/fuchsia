// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/i2c-impl.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "i2c-impl-internal.h"

// DDK I2C implementation protocol support.
//
// :: Proxies ::
//
// ddk::I2cImplProtocolProxy is a simple wrappers around i2c_impl_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::I2cImplProtocol is a mixin class that simplifies writing DDK drivers that
// implement the I2C implementation protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_I2C_IMPL device.
// class I2cImplDevice;
// using I2cImplDeviceType = ddk::Device<I2cImplDevice, /* ddk mixins */>;
//
// class I2cImplDevice : public I2cImplDeviceType,
//                       public ddk::I2cImplProtocol<I2cImplDevice> {
//   public:
//     I2cImplDevice(zx_device_t* parent)
//       : I2cImplDeviceType("my-i2c-impl-device", parent) {}
//
//    zx_status_t Transact(uint32_t bus_id, uint16_t address, i2c_impl_op_t* ops, size_t count);
//    zx_status_t I2cGetMaxTransferSize(uint32_t index, size_t* out_size);
//     ...
// };

namespace ddk {

template <typename D>
class I2cImplProtocol : public internal::base_protocol {
public:
    I2cImplProtocol() {
        internal::CheckI2cImplProtocolSubclass<D>();
        ops_.get_bus_count = I2cImplGetBusCount;
        ops_.get_max_transfer_size = I2cImplGetMaxTransferSize;
        ops_.set_bitrate = I2cImplSetBitRate;
        ops_.transact = I2cImplTransact;

        // Can only inherit from one base_protocol implementation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_I2C_IMPL;
        ddk_proto_ops_ = &ops_;
    }

protected:
    i2c_impl_protocol_ops_t ops_ = {};

private:
    static uint32_t I2cImplGetBusCount(void* ctx) {
        return static_cast<D*>(ctx)->I2cImplGetBusCount();
    }
    static zx_status_t I2cImplGetMaxTransferSize(void* ctx, uint32_t bus_id, size_t* out_size) {
        return static_cast<D*>(ctx)->I2cImplGetMaxTransferSize(bus_id, out_size);
    }
    static zx_status_t I2cImplSetBitRate(void* ctx, uint32_t bus_id, uint32_t bitrate) {
        return static_cast<D*>(ctx)->I2cImplSetBitRate(bus_id, bitrate);
    }
    static zx_status_t I2cImplTransact(void* ctx, uint32_t bus_id, i2c_impl_op_t* ops,
                                       size_t count) {
        return static_cast<D*>(ctx)->I2cImplTransact(bus_id, ops, count);
    }
};

class I2cImplProtocolProxy {
public:
    I2cImplProtocolProxy(i2c_impl_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(i2c_impl_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }

    uint32_t GetBusCount() {
        return ops_->get_bus_count(ctx_);
    }
    zx_status_t GetMaxTransferSize(uint32_t bus_id, size_t* out_size) {
        return ops_->get_max_transfer_size(ctx_, bus_id, out_size);
    }
    zx_status_t SetBitRate(uint32_t bus_id, uint32_t bitrate) {
        return ops_->set_bitrate(ctx_, bus_id, bitrate);
    }
    zx_status_t Transact(uint32_t bus_id, i2c_impl_op_t* ops, size_t count) {
        return ops_->transact(ctx_, bus_id, ops, count);
    }

private:
    i2c_impl_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
