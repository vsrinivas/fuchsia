// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <lib/device-protocol/i2c-channel.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <fuchsia/hardware/backlight/c/fidl.h>

namespace backlight {

class Sgm37603a;
using DeviceType = ddk::Device<Sgm37603a, ddk::Messageable>;

class Sgm37603a : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_BACKLIGHT> {
public:
    virtual ~Sgm37603a() = default;

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    void DdkRelease() { delete this; }

    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    // Visible for testing.
    Sgm37603a(zx_device_t* parent, ddk::I2cChannel i2c, ddk::GpioProtocolClient reset_gpio)
        : DeviceType(parent), i2c_(i2c), reset_gpio_(reset_gpio) {}

    virtual zx_status_t EnableBacklight();
    virtual zx_status_t DisableBacklight();

    zx_status_t GetBacklightState(bool* power, uint8_t* brightness);
    zx_status_t SetBacklightState(bool power, uint8_t brightness);

private:
    static zx_status_t GetState(void* ctx, fidl_txn_t* txn);
    static zx_status_t SetState(void* ctx, const fuchsia_hardware_backlight_State* state);

    static constexpr fuchsia_hardware_backlight_Device_ops_t fidl_ops_ = {
        .GetState = GetState,
        .SetState = SetState,
    };

    ddk::I2cChannel i2c_;
    ddk::GpioProtocolClient reset_gpio_;
    bool enabled_ = false;
    uint8_t brightness_ = 0;
};

}  // namespace backlight
