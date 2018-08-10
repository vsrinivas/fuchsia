// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "aml-pwm.h"
#include "aml-tsensor.h"
#include <ddk/device.h>
#include <ddktl/device.h>
#include <fbl/unique_ptr.h>

namespace thermal {

class AmlThermal;
using DeviceType = ddk::Device<AmlThermal,
                               ddk::Unbindable,
                               ddk::Ioctlable>;

class AmlThermal : public DeviceType,
                   public ddk::internal::base_protocol {

public:
    AmlThermal(zx_device_t* device, fbl::unique_ptr<thermal::AmlTSensor> tsensor,
               fbl::unique_ptr<thermal::AmlPwm> pwm)
        : DeviceType(device), tsensor_(fbl::move(tsensor)), pwm_(fbl::move(pwm)) {
        ddk_proto_id_ = ZX_PROTOCOL_THERMAL;
    };
    static zx_status_t Create(zx_device_t* device);

    // Ddk Hooks
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

private:
    fbl::unique_ptr<thermal::AmlTSensor> tsensor_;
    fbl::unique_ptr<thermal::AmlPwm> pwm_;
};
} // namespace thermal
