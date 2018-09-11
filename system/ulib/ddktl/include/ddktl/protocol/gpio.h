// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/gpio.banjo INSTEAD.

#pragma once

#include <ddk/protocol/gpio.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "gpio-internal.h"

// DDK gpio-protocol support
//
// :: Proxies ::
//
// ddk::GpioProtocolProxy is a simple wrapper around
// gpio_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::GpioProtocol is a mixin class that simplifies writing DDK drivers
// that implement the gpio protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_GPIO device.
// class GpioDevice {
// using GpioDeviceType = ddk::Device<GpioDevice, /* ddk mixins */>;
//
// class GpioDevice : public GpioDeviceType,
//                    public ddk::GpioProtocol<GpioDevice> {
//   public:
//     GpioDevice(zx_device_t* parent)
//         : GpioDeviceType("my-gpio-protocol-device", parent) {}
//
//     zx_status_t GpioConfigIn(uint32_t flags);
//
//     zx_status_t GpioConfigOut(uint8_t initial_value);
//
//     zx_status_t GpioSetAltFunction(uint64_t function);
//
//     zx_status_t GpioRead(uint8_t* out_value);
//
//     zx_status_t GpioWrite(uint8_t value);
//
//     zx_status_t GpioGetInterrupt(uint32_t flags, zx_handle_t* out_irq);
//
//     zx_status_t GpioReleaseInterrupt();
//
//     zx_status_t GpioSetPolarity(gpio_polarity_t polarity);
//
//     ...
// };

namespace ddk {

template <typename D>
class GpioProtocol : public internal::base_mixin {
public:
    GpioProtocol() {
        internal::CheckGpioProtocolSubclass<D>();
        gpio_protocol_ops_.config_in = GpioConfigIn;
        gpio_protocol_ops_.config_out = GpioConfigOut;
        gpio_protocol_ops_.set_alt_function = GpioSetAltFunction;
        gpio_protocol_ops_.read = GpioRead;
        gpio_protocol_ops_.write = GpioWrite;
        gpio_protocol_ops_.get_interrupt = GpioGetInterrupt;
        gpio_protocol_ops_.release_interrupt = GpioReleaseInterrupt;
        gpio_protocol_ops_.set_polarity = GpioSetPolarity;
    }

protected:
    gpio_protocol_ops_t gpio_protocol_ops_ = {};

private:
    // Configures a GPIO for input.
    static zx_status_t GpioConfigIn(void* ctx, uint32_t flags) {
        return static_cast<D*>(ctx)->GpioConfigIn(flags);
    }
    // Configures a GPIO for output.
    static zx_status_t GpioConfigOut(void* ctx, uint8_t initial_value) {
        return static_cast<D*>(ctx)->GpioConfigOut(initial_value);
    }
    // Configures the GPIO pin for an alternate function (I2C, SPI, etc)
    // the interpretation of "function" is platform dependent.
    static zx_status_t GpioSetAltFunction(void* ctx, uint64_t function) {
        return static_cast<D*>(ctx)->GpioSetAltFunction(function);
    }
    // Reads the current value of a GPIO (0 or 1).
    static zx_status_t GpioRead(void* ctx, uint8_t* out_value) {
        return static_cast<D*>(ctx)->GpioRead(out_value);
    }
    // Sets the current value of the GPIO (any non-zero value maps to 1).
    static zx_status_t GpioWrite(void* ctx, uint8_t value) {
        return static_cast<D*>(ctx)->GpioWrite(value);
    }
    // Gets an interrupt object pertaining to a particular GPIO pin.
    static zx_status_t GpioGetInterrupt(void* ctx, uint32_t flags, zx_handle_t* out_irq) {
        return static_cast<D*>(ctx)->GpioGetInterrupt(flags, out_irq);
    }
    // Release the interrupt.
    static zx_status_t GpioReleaseInterrupt(void* ctx) {
        return static_cast<D*>(ctx)->GpioReleaseInterrupt();
    }
    // Set GPIO polarity.
    static zx_status_t GpioSetPolarity(void* ctx, gpio_polarity_t polarity) {
        return static_cast<D*>(ctx)->GpioSetPolarity(polarity);
    }
};

class GpioProtocolProxy {
public:
    GpioProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    GpioProtocolProxy(const gpio_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(gpio_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Configures a GPIO for input.
    zx_status_t ConfigIn(uint32_t flags) { return ops_->config_in(ctx_, flags); }
    // Configures a GPIO for output.
    zx_status_t ConfigOut(uint8_t initial_value) { return ops_->config_out(ctx_, initial_value); }
    // Configures the GPIO pin for an alternate function (I2C, SPI, etc)
    // the interpretation of "function" is platform dependent.
    zx_status_t SetAltFunction(uint64_t function) { return ops_->set_alt_function(ctx_, function); }
    // Reads the current value of a GPIO (0 or 1).
    zx_status_t Read(uint8_t* out_value) { return ops_->read(ctx_, out_value); }
    // Sets the current value of the GPIO (any non-zero value maps to 1).
    zx_status_t Write(uint8_t value) { return ops_->write(ctx_, value); }
    // Gets an interrupt object pertaining to a particular GPIO pin.
    zx_status_t GetInterrupt(uint32_t flags, zx_handle_t* out_irq) {
        return ops_->get_interrupt(ctx_, flags, out_irq);
    }
    // Release the interrupt.
    zx_status_t ReleaseInterrupt() { return ops_->release_interrupt(ctx_); }
    // Set GPIO polarity.
    zx_status_t SetPolarity(gpio_polarity_t polarity) { return ops_->set_polarity(ctx_, polarity); }

private:
    gpio_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
