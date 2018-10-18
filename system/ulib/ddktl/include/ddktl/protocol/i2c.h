// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/i2c.banjo INSTEAD.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "i2c-internal.h"

// DDK i2c-protocol support
//
// :: Proxies ::
//
// ddk::I2cProtocolProxy is a simple wrapper around
// i2c_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::I2cProtocol is a mixin class that simplifies writing DDK drivers
// that implement the i2c protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_I2C device.
// class I2cDevice {
// using I2cDeviceType = ddk::Device<I2cDevice, /* ddk mixins */>;
//
// class I2cDevice : public I2cDeviceType,
//                   public ddk::I2cProtocol<I2cDevice> {
//   public:
//     I2cDevice(zx_device_t* parent)
//         : I2cDeviceType("my-i2c-protocol-device", parent) {}
//
//     void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
//     void* cookie);
//
//     zx_status_t I2cGetMaxTransferSize(size_t* out_size);
//
//     zx_status_t I2cGetInterrupt(uint32_t flags, zx_handle_t* out_irq);
//
//     ...
// };

namespace ddk {

template <typename D>
class I2cProtocol : public internal::base_mixin {
public:
    I2cProtocol() {
        internal::CheckI2cProtocolSubclass<D>();
        i2c_protocol_ops_.transact = I2cTransact;
        i2c_protocol_ops_.get_max_transfer_size = I2cGetMaxTransferSize;
        i2c_protocol_ops_.get_interrupt = I2cGetInterrupt;
    }

protected:
    i2c_protocol_ops_t i2c_protocol_ops_ = {};

private:
    // Writes and reads data on an i2c channel. Up to I2C_MAX_RW_OPS operations can be passed in.
    // For write ops, i2c_op_t.data points to data to write.  The data to write does not need to be
    // kept alive after this call.  For read ops, i2c_op_t.data is ignored.  Any combination of
    // reads and writes can be specified.  At least the last op must have the stop flag set. The
    // results of the operations are returned asynchronously via the transact_cb. The cookie
    // parameter can be used to pass your own private data to the transact_cb callback.
    static void I2cTransact(void* ctx, const i2c_op_t* op_list, size_t op_count,
                            i2c_transact_callback callback, void* cookie) {
        static_cast<D*>(ctx)->I2cTransact(op_list, op_count, callback, cookie);
    }
    // Returns the maximum transfer size for read and write operations on the channel.
    static zx_status_t I2cGetMaxTransferSize(void* ctx, size_t* out_size) {
        return static_cast<D*>(ctx)->I2cGetMaxTransferSize(out_size);
    }
    static zx_status_t I2cGetInterrupt(void* ctx, uint32_t flags, zx_handle_t* out_irq) {
        return static_cast<D*>(ctx)->I2cGetInterrupt(flags, out_irq);
    }
};

class I2cProtocolProxy {
public:
    I2cProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    I2cProtocolProxy(const i2c_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(i2c_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Writes and reads data on an i2c channel. Up to I2C_MAX_RW_OPS operations can be passed in.
    // For write ops, i2c_op_t.data points to data to write.  The data to write does not need to be
    // kept alive after this call.  For read ops, i2c_op_t.data is ignored.  Any combination of
    // reads and writes can be specified.  At least the last op must have the stop flag set. The
    // results of the operations are returned asynchronously via the transact_cb. The cookie
    // parameter can be used to pass your own private data to the transact_cb callback.
    void Transact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                  void* cookie) {
        ops_->transact(ctx_, op_list, op_count, callback, cookie);
    }
    // Returns the maximum transfer size for read and write operations on the channel.
    zx_status_t GetMaxTransferSize(size_t* out_size) {
        return ops_->get_max_transfer_size(ctx_, out_size);
    }
    zx_status_t GetInterrupt(uint32_t flags, zx_handle_t* out_irq) {
        return ops_->get_interrupt(ctx_, flags, out_irq);
    }

private:
    i2c_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
