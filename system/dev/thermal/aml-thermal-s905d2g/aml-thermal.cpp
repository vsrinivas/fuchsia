// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"
#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <threads.h>
#include <zircon/device/thermal.h>

namespace thermal {

zx_status_t AmlThermal::Create(zx_device_t* device) {
    auto tsensor = fbl::make_unique<thermal::AmlTSensor>();
    // Create a PWM period = 1250, hwpwm - 1 to signify using PWM_D from PWM_C/D
    // Source: Amlogic SDK
    auto pwm = fbl::make_unique<thermal::AmlPwm>(1250, 1);

    // Initialize Temperature Sensor.
    zx_status_t status = tsensor->InitSensor(device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-thermal: Could not inititalize Temperature Sensor: %d\n", status);
        return status;
    }

    // Initialize the PWM.
    status = pwm->Init(device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-thermal: Could not inititalize PWM: %d\n", status);
        return status;
    }

    auto thermal_device = fbl::make_unique<thermal::AmlThermal>(device, fbl::move(tsensor),
                                                                fbl::move(pwm));

    status = thermal_device->DdkAdd("thermal");
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-thermal: Could not create thermal device: %d\n", status);
        return status;
    }

    // devmgr is now in charge of the memory for dev.
    __UNUSED auto ptr = thermal_device.release();
    return ZX_OK;
}

zx_status_t AmlThermal::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_THERMAL_GET_TEMPERATURE: {
        if (out_len != sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto temperature = static_cast<uint32_t*>(out_buf);
        *temperature = tsensor_->ReadTemperature();
        *out_actual = sizeof(uint32_t);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void AmlThermal::DdkUnbind() {
    DdkRemove();
}

void AmlThermal::DdkRelease() {
    delete this;
}

} // namespace thermal

extern "C" zx_status_t aml_thermal(void* ctx, zx_device_t* device) {
    return thermal::AmlThermal::Create(device);
}
