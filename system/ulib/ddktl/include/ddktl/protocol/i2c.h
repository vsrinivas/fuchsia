// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "i2c-internal.h"

// DDK I2C protocol support.
//
// :: Proxies ::
//
// ddk::I2cProtocolProxy is a simple wrappers around i2c_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::I2cProtocol is a mixin class that simplifies writing DDK drivers that
// implement the I2C protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_I2C device.
// class I2cDevice;
// using I2cDeviceType = ddk::Device<I2cDevice, /* ddk mixins */>;
//
// class I2cDevice : public I2cDeviceType,
//                   public ddk::I2cProtocol<I2cDevice> {
//   public:
//     I2cDevice(zx_device_t* parent)
//       : I2cDeviceType("my-i2c-device", parent) {}
//
//    zx_status_t I2cTransact(uint32_t index, const void* write_buf, size_t write_length,
//                            size_t read_length, i2c_complete_cb complete_cb, void* cookie);
//    zx_status_t I2cGetMaxTransferSize(uint32_t index, size_t* out_size);
//     ...
// };

namespace ddk {

template <typename D>
class I2cProtocol {
public:
    I2cProtocol() {
        internal::CheckI2cProtocolSubclass<D>();
        i2c_proto_ops_.transact = I2cTransact;
        i2c_proto_ops_.get_max_transfer_size = I2cGetMaxTransferSize;
    }

protected:
    i2c_protocol_ops_t i2c_proto_ops_ = {};

private:
    static zx_status_t I2cTransact(void* ctx, uint32_t index, const void* write_buf,
                                   size_t write_length, size_t read_length,
                                   i2c_complete_cb complete_cb, void* cookie) {
        return static_cast<D*>(ctx)->I2cTransact(index, write_buf, write_length, read_length,
                                                 complete_cb, cookie);
    }
    static zx_status_t I2cGetMaxTransferSize(void* ctx, uint32_t index, size_t* out_size) {
        return static_cast<D*>(ctx)->I2cGetMaxTransferSize(index, out_size);
    }
};

class I2cProtocolProxy {
public:
    I2cProtocolProxy(i2c_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t Transact(uint32_t index, const void* write_buf, size_t write_length,
                            size_t read_length, i2c_complete_cb complete_cb, void* cookie) {
        return ops_->transact(ctx_, index, write_buf, write_length, read_length, complete_cb,
                              cookie);
    }
    zx_status_t GetMaxTransferSize(uint32_t index, size_t* out_size) {
        return ops_->get_max_transfer_size(ctx_, index, out_size);
    }

private:
    i2c_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
