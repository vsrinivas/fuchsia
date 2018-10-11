// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/gpio.h>
#include <fbl/macros.h>
#include <lib/zx/interrupt.h>
#include <zircon/assert.h>

namespace ddk {

class GpioPin {

public:
    friend class Pdev;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(GpioPin);
    DISALLOW_NEW;

    GpioPin() = default;

    GpioPin(GpioPin&& other) {
        gpio_.ops = other.gpio_.ops;
        gpio_.ctx = other.gpio_.ctx;
        other.reset();
    }

    ~GpioPin() {}

    // Allow assignment from an rvalue
    GpioPin& operator=(GpioPin&& other) {
        gpio_.ops = other.gpio_.ops;
        gpio_.ctx = other.gpio_.ctx;
        other.reset();
        return *this;
    }

    void reset() {
        gpio_ = {0, 0};
    }

    /*
        All of the member methods assume that the GpioPin instance has been
        properly initialed/constructed via the friend class Pdev.  If any of
        the calls below are done on an uninitialized instance it will result in
        a crash.

        is_valid() can be used to determine of GpioPin instance has been
        properly initialized.
    */
    zx_status_t Read(uint8_t* out) const {
        return gpio_read(&gpio_, out);
    }

    zx_status_t Write(uint8_t val) const {
        return gpio_write(&gpio_, val);
    }

    zx_status_t ConfigIn(uint32_t flags) const {
        return gpio_config_in(&gpio_, flags);
    }

    zx_status_t ConfigOut(uint8_t initial_value) const {
        return gpio_config_out(&gpio_, initial_value);
    }

    zx_status_t SetFunction(uint64_t function) const {
        return gpio_set_alt_function(&gpio_, function);
    }

    zx_status_t GetInterrupt(uint32_t flags, zx::interrupt* out) const {
        return gpio_get_interrupt(&gpio_, flags,
                                  out->reset_and_get_address());
    }

    zx_status_t SetPolarity(uint32_t polarity) const {
        return gpio_set_polarity(&gpio_, polarity);
    }

    // Check to determine if this object is initialized
    bool is_valid() const {
        return (gpio_.ops && gpio_.ctx);
    }

private:
    /*
        Users must use the Pdev class as a factory for GpioPin
        instances, hence the meaningful constructor(s) are buried.
    */
    GpioPin(gpio_protocol_t gpio)
        : gpio_(gpio) {
    }

    gpio_protocol_t gpio_ = {nullptr, nullptr};
};

} //namespace ddk
