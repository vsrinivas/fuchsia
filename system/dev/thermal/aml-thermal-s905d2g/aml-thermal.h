// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "aml-cpufreq.h"
#include "aml-pwm.h"
#include "aml-tsensor.h"
#include "aml-voltage.h"
#include <ddk/device.h>
#include <ddktl/device.h>
#include <fbl/atomic.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

#include <utility>

namespace thermal {

class AmlThermal;
using DeviceType = ddk::Device<AmlThermal,
                               ddk::Unbindable,
                               ddk::Ioctlable>;

class AmlThermal : public DeviceType,
                   public ddk::internal::base_protocol {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlThermal);
    AmlThermal(zx_device_t* device, fbl::unique_ptr<thermal::AmlTSensor> tsensor,
               fbl::unique_ptr<thermal::AmlVoltageRegulator> voltage_regulator,
               fbl::unique_ptr<thermal::AmlCpuFrequency> cpufreq_scaling,
               opp_info_t opp_info,
               thermal_device_info_t thermal_config)
        : DeviceType(device), tsensor_(std::move(tsensor)),
          voltage_regulator_(std::move(voltage_regulator)),
          cpufreq_scaling_(std::move(cpufreq_scaling)),
          opp_info_(std::move(opp_info)),
          thermal_config_(std::move(thermal_config)) {
        ddk_proto_id_ = ZX_PROTOCOL_THERMAL;
    };
    static zx_status_t Create(zx_device_t* device);

    // Ddk Hooks
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

private:
    int ThermalNotificationThread();
    zx_status_t NotifyThermalDaemon();
    zx_status_t SetTarget(uint32_t opp_idx);

    fbl::unique_ptr<thermal::AmlTSensor> tsensor_;
    fbl::unique_ptr<thermal::AmlVoltageRegulator> voltage_regulator_;
    fbl::unique_ptr<thermal::AmlCpuFrequency> cpufreq_scaling_;
    opp_info_t opp_info_;
    thermal_device_info_t thermal_config_;
};
} // namespace thermal
