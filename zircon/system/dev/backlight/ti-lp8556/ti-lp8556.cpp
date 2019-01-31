// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ti-lp8556.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/platform/bus.h>

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

namespace ti {

namespace {
constexpr uint8_t kBacklightControlReg =  0x0;
constexpr uint8_t kDeviceControlReg = 0x1;
constexpr uint8_t kCfg2Reg = 0xA2;

constexpr uint8_t kBacklightOn = 0x05;
constexpr uint8_t kBacklightOff = 0x04;
constexpr uint8_t kCfg2Default = 0x20;
} // namespace

zx_status_t Lp8556Device::Bind() {
    zx_status_t status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev_);
    if (status != ZX_OK) {
        LOG_ERROR("Could not get parent protocol\n");
        return status;
    }

    // Obtain I2C protocol needed to control backlight
    status = device_get_protocol(parent_, ZX_PROTOCOL_I2C, &i2c_);
    if (status != ZX_OK) {
        LOG_ERROR("Could not obtain I2C protocol\n");
        return status;
    }

    status = DdkAdd("ti-lp8556");
    if (status != ZX_OK) {
        LOG_ERROR("Could not add device\n");
        return status;
    }
    return ZX_OK;
}


void Lp8556Device::DdkUnbind() {
    DdkRemove();
}

void Lp8556Device::DdkRelease() {
    delete this;
}

zx_status_t Lp8556Device::GetBacklightState(bool* power, uint8_t* brightness) {
    *power = power_;
    *brightness = brightness_;
    return ZX_OK;
}

zx_status_t Lp8556Device::SetBacklightState(bool power, uint8_t brightness) {
    if (brightness != brightness_) {
        uint8_t buf[2];
        buf[0] = kBacklightControlReg;
        buf[1] = brightness;
        i2c_write_sync(&i2c_, buf, 2);
    }

    if (power != power_) {
        uint8_t buf[2];
        buf[0] = kDeviceControlReg;
        buf[1] = power? kBacklightOn : kBacklightOff;
        i2c_write_sync(&i2c_, buf, 2);
        if (power) {
            buf[0] = kCfg2Reg;
            buf[1] = kCfg2Default;
            i2c_write_sync(&i2c_, buf, 2);
        }
    }

    // update internal values
    power_ = power;
    brightness_ = brightness;
    return ZX_OK;
}

static zx_status_t GetState(void* ctx, fidl_txn_t* txn) {
    fuchsia_hardware_backlight_State state;
    auto& self = *static_cast<ti::Lp8556Device*>(ctx);
    self.GetBacklightState(&state.on, &state.brightness);
    return fuchsia_hardware_backlight_DeviceGetState_reply(txn, &state);
}

static zx_status_t SetState(void* ctx, const fuchsia_hardware_backlight_State* state) {
    auto& self = *static_cast<ti::Lp8556Device*>(ctx);
    self.SetBacklightState(state->on, state->brightness);
    return ZX_OK;
}

static fuchsia_hardware_backlight_Device_ops_t fidl_ops = {
    .GetState = GetState,
    .SetState = SetState,
};

zx_status_t Lp8556Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_backlight_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t ti_lp8556_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<ti::Lp8556Device>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = dev->Bind();
    if (status != ZX_OK) {
        // devmgr is now in charge of memory for dev
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

static zx_driver_ops_t ti_lp8556_driver_ops = [](){
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = ti_lp8556_bind;
    return ops;
}();

} // namespace ti

// clang-format off
ZIRCON_DRIVER_BEGIN(ti_lp8556, ti::ti_lp8556_driver_ops, "TI-LP8556", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TI),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_TI_LP8556),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TI_BACKLIGHT),
ZIRCON_DRIVER_END(ti_lp8556)
// clang-format on
