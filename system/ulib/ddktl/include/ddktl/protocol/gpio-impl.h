// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/gpio-impl.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

#include "gpio-impl-internal.h"

// DDK GPIO implementation protocol support.
//
// :: Proxies ::
//
// ddk::GpioImplProtocolProxy is a simple wrappers around gpio_impl_protocol_t. It does
// not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::GpioImplProtocol is a mixin class that simplifies writing DDK drivers that
// implement the GPIO implementation protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_GPIO_IMPL device.
// class GpioImplDevice;
// using GpioImplDeviceType = ddk::Device<GpioImplDevice, /* ddk mixins */>;
//
// class GpioImplDevice : public GpioImplDeviceType,
//                        public ddk::GpioImplProtocol<GpioImplDevice> {
//   public:
//     GpioImplDevice(zx_device_t* parent)
//       : GpioImplDeviceType("my-GpioImpl-device", parent) {}
//
//     zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
//     zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
//     zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
//     zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
//     zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
//     zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t *out_handle);
//     zx_status_t GpioImplReleaseInterrupt(uint32_t index);
//     zx_status_t GpioImplSetPolarity(uint32_t index, uint32_t polarity);
//     ...
// };

namespace ddk {

template <typename D>
class GpioImplProtocol : public internal::base_protocol {
public:
    GpioImplProtocol() {
        internal::CheckGpioImplProtocolSubclass<D>();
        ops_.config_in = GpioImplConfigIn;
        ops_.config_out = GpioImplConfigOut;
        ops_.set_alt_function = GpioImplSetAltFunction;
        ops_.read = GpioImplRead;
        ops_.write = GpioImplWrite;
        ops_.get_interrupt = GpioImplGetInterrupt;
        ops_.release_interrupt = GpioImplReleaseInterrupt;
        ops_.set_polarity = GpioImplSetPolarity;

        // Can only inherit from one base_protocol implemenation
        ZX_ASSERT(ddk_proto_id_ == 0);
        ddk_proto_id_ = ZX_PROTOCOL_GPIO_IMPL;
        ddk_proto_ops_ = &ops_;
    }

protected:
    gpio_impl_protocol_ops_t ops_ = {};

private:
    static zx_status_t GpioImplConfigIn(void* ctx, uint32_t index, uint32_t flags) {
        return static_cast<D*>(ctx)->GpioImplConfigIn(index, flags);
    }
    static zx_status_t GpioImplConfigOut(void* ctx, uint32_t index, uint8_t initial_value) {
        return static_cast<D*>(ctx)->GpioImplConfigOut(index, initial_value);
    }
    static zx_status_t GpioImplSetAltFunction(void* ctx, uint32_t index, uint64_t function) {
        return static_cast<D*>(ctx)->GpioImplSetAltFunction(index, function);
    }
    static zx_status_t GpioImplRead(void* ctx, uint32_t index, uint8_t* out_value) {
        return static_cast<D*>(ctx)->GpioImplRead(index, out_value);
    }
    static zx_status_t GpioImplWrite(void* ctx, uint32_t index, uint8_t value) {
        return static_cast<D*>(ctx)->GpioImplWrite(index, value);
    }
    static zx_status_t GpioImplGetInterrupt(void* ctx, uint32_t index, uint32_t flags,
                                        zx_handle_t* out_handle) {
        return static_cast<D*>(ctx)->GpioImplGetInterrupt(index, flags, out_handle);
    }
    static zx_status_t GpioImplReleaseInterrupt(void* ctx, uint32_t index) {
        return static_cast<D*>(ctx)->GpioImplReleaseInterrupt(index);
    }
    static zx_status_t GpioImplSetPolarity(void* ctx, uint32_t index, uint32_t polarity) {
        return static_cast<D*>(ctx)->GpioImplSetPolarity(index, polarity);
    }
};

class GpioImplProtocolProxy {
public:
    GpioImplProtocolProxy(gpio_impl_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(gpio_impl_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }

    zx_status_t ConfigIn(uint32_t index, uint32_t flags) {
        return ops_->config_in(ctx_, index, flags);
    }
    zx_status_t ConfigOut(uint32_t index, uint8_t initial_value) {
        return ops_->config_out(ctx_, index, initial_value);
    }
    zx_status_t SetAltFunction(uint32_t index, uint64_t function) {
        return ops_->set_alt_function(ctx_, index, function);
    }
    zx_status_t Read(uint32_t index, uint8_t* out_value) {
        return ops_->read(ctx_, index, out_value);
    }
    zx_status_t Write(uint32_t index, uint8_t value) {
        return ops_->write(ctx_, index, value);
    }
    zx_status_t GetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle) {
        return ops_->get_interrupt(ctx_, index, flags, out_handle);
    }
    zx_status_t ReleaseInterrupt(uint32_t index) {
        return ops_->release_interrupt(ctx_, index);
    }
    zx_status_t SetPolarity(uint32_t index, uint32_t polarity) {
        return ops_->set_polarity(ctx_, index, polarity);
    }

private:
    gpio_impl_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
