// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
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
//    zx_status_t I2cTransact(i2c_op_t* ops, size_t cnt,
//                            i2c_transact_cb transact_cb, void* cookie);
//    zx_status_t I2cGetMaxTransferSize(size_t* out_size);
//     ...
// };

namespace ddk {

template <typename D>
class I2cProtocol : public internal::base_protocol {
public:
    I2cProtocol() {
        internal::CheckI2cProtocolSubclass<D>();
        ops_.transact = I2cTransact;
        ops_.get_max_transfer_size = I2cGetMaxTransferSize;

        // Can only inherit from one base_protocol implementation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_I2C;
        ddk_proto_ops_ = &ops_;
    }

protected:
    i2c_protocol_ops_t ops_ = {};

private:
    static zx_status_t I2cTransact(void* ctx, i2c_op_t* ops, size_t cnt,
                                   i2c_transact_cb transact_cb, void* cookie) {
        return static_cast<D*>(ctx)->I2cTransact(ops, cnt, transact_cb, cookie);
    }
    static zx_status_t I2cGetMaxTransferSize(void* ctx, size_t* out_size) {
        return static_cast<D*>(ctx)->I2cGetMaxTransferSize(out_size);
    }
    static zx_status_t I2cGetInterrupt(void* ctx, uint32_t flags, zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->I2cGetInterrupt(flags, out_handle);
    }
};

class I2cProtocolProxy {
public:
    I2cProtocolProxy(i2c_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    zx_status_t Transact(i2c_op_t* ops, size_t cnt, i2c_transact_cb transact_cb, void* cookie) {
        return ops_->transact(ctx_, ops, cnt, transact_cb, cookie);
    }
    zx_status_t GetMaxTransferSize(size_t* out_size) {
        return ops_->get_max_transfer_size(ctx_, out_size);
    }
    zx_status_t GetInterrupt(uint32_t flags, zx_handle_t* out_handle) {
        return ops_->get_interrupt(ctx_, flags, out_handle);
    }

private:
    i2c_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
