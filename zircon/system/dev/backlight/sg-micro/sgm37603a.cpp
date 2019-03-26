// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sgm37603a.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/pdev.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

namespace {

constexpr int64_t kEnableSleepTimeMs = 20;

}  // namespace

namespace {

constexpr uint8_t kEnable = 0x10;
constexpr uint8_t kEnableDevice = 0x01;
constexpr uint8_t kEnableLed1 = 0x02;

constexpr uint8_t kBrightnessControl = 0x11;
constexpr uint8_t kBrightnessControlRegisterOnly = 0x00;
constexpr uint8_t kBrightnessControlRampDisabled = 0x00;

constexpr uint8_t kBrightnessLsb = 0x1a;
constexpr uint8_t kBrightnessMsb = 0x19;

constexpr uint8_t kDefaultRegValues[][2] = {
    {kEnable, kEnableDevice | kEnableLed1},
    {kBrightnessControl, kBrightnessControlRegisterOnly | kBrightnessControlRampDisabled},
    {kBrightnessLsb, 0},
    {kBrightnessMsb, 0},
};

}  // namespace

namespace backlight {

zx_status_t Sgm37603a::Create(void* ctx, zx_device_t* parent) {
    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s: Failed to get pdev\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    ddk::I2cChannel i2c = pdev.GetI2c(0);
    if (!i2c.is_valid()) {
        zxlogf(ERROR, "%s: Failed to get I2C\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    ddk::GpioProtocolClient reset_gpio = pdev.GetGpio(0);
    if (!reset_gpio.is_valid()) {
        zxlogf(ERROR, "%s: Failed to get GPIO\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Sgm37603a> device(new (&ac) Sgm37603a(parent, i2c, reset_gpio));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Sgm37603a alloc failed\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device->SetBacklightState(true, 255);
    if (status != ZX_OK) {
        return status;
    }

    if ((status = device->DdkAdd("sgm37603a")) != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
        return status;
    }

    __UNUSED auto* dummy = device.release();

    return ZX_OK;
}

zx_status_t Sgm37603a::EnableBacklight() {
    zx_status_t status = reset_gpio_.ConfigOut(1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to enable backlight driver\n", __FILE__);
        return status;
    }

    zx::nanosleep(zx::deadline_after(zx::msec(kEnableSleepTimeMs)));

    for (size_t i = 0; i < countof(kDefaultRegValues); i++) {
        status = i2c_.WriteSync(kDefaultRegValues[i], sizeof(kDefaultRegValues[i]));
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Failed to configure backlight driver\n", __FILE__);
            return status;
        }
    }

    return ZX_OK;
}

zx_status_t Sgm37603a::DisableBacklight() {
    zx_status_t status = reset_gpio_.ConfigOut(0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to disable backlight driver\n", __FILE__);
        return status;
    }

    return ZX_OK;
}

zx_status_t Sgm37603a::GetState(void* ctx, fidl_txn_t* txn) {
    fuchsia_hardware_backlight_State state;
    auto& self = *static_cast<backlight::Sgm37603a*>(ctx);
    self.GetBacklightState(&state.on, &state.brightness);
    return fuchsia_hardware_backlight_DeviceGetState_reply(txn, &state);
}

zx_status_t Sgm37603a::SetState(void* ctx, const fuchsia_hardware_backlight_State* state) {
    auto& self = *static_cast<backlight::Sgm37603a*>(ctx);
    self.SetBacklightState(state->on, state->brightness);
    return ZX_OK;
}

zx_status_t Sgm37603a::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_backlight_Device_dispatch(this, txn, msg, &fidl_ops_);
}

zx_status_t Sgm37603a::GetBacklightState(bool* power, uint8_t* brightness) {
    *power = enabled_;
    *brightness = brightness_;
    return ZX_OK;
}

zx_status_t Sgm37603a::SetBacklightState(bool power, uint8_t brightness) {
    if (!power) {
        enabled_ = false;
        brightness_ = 0;

        return DisableBacklight();
    } else if (!enabled_) {
        enabled_ = true;

        zx_status_t status = EnableBacklight();
        if (status != ZX_OK) {
            return status;
        }
    }

    const uint8_t brightness_regs[][2] = {
        {kBrightnessLsb, 0},
        {kBrightnessMsb, brightness},
    };

    for (size_t i = 0; i < countof(brightness_regs); i++) {
        zx_status_t status = i2c_.WriteSync(brightness_regs[i], sizeof(brightness_regs[i]));
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Failed to set brightness register\n", __FILE__);
            return status;
        }
    }

    brightness_ = brightness;
    return ZX_OK;
}

}  // namespace backlight

static zx_driver_ops_t sgm37603a_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = backlight::Sgm37603a::Create;
    return ops;
}();

ZIRCON_DRIVER_BEGIN(sgm37603a, sgm37603a_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_SG_MICRO_SGM37603A),
ZIRCON_DRIVER_END(sgm37603a)
