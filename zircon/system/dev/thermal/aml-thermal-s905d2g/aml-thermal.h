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
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <threads.h>

#include <utility>

namespace thermal {

class AmlThermal;
using DeviceType = ddk::Device<AmlThermal,
                               ddk::Unbindable,
                               ddk::Ioctlable,
                               ddk::Messageable>;

class AmlThermal : public DeviceType,
                   public ddk::EmptyProtocol<ZX_PROTOCOL_THERMAL> {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlThermal);
    AmlThermal(zx_device_t* device, fbl::unique_ptr<thermal::AmlTSensor> tsensor,
               fbl::unique_ptr<thermal::AmlVoltageRegulator> voltage_regulator,
               fbl::unique_ptr<thermal::AmlCpuFrequency> cpufreq_scaling,
               opp_info_t opp_info,
               fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config)
        : DeviceType(device), tsensor_(std::move(tsensor)),
          voltage_regulator_(std::move(voltage_regulator)),
          cpufreq_scaling_(std::move(cpufreq_scaling)),
          opp_info_(std::move(opp_info)),
          thermal_config_(std::move(thermal_config)) {}

    static zx_status_t Create(zx_device_t* device);

    // Ddk Hooks
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

private:
    zx_status_t GetInfo(fidl_txn_t* txn);
    zx_status_t GetDeviceInfo(fidl_txn_t* txn);
    zx_status_t GetDvfsInfo(fuchsia_hardware_thermal_PowerDomain power_domain, fidl_txn_t* txn);
    zx_status_t GetTemperature(fidl_txn_t* txn);
    zx_status_t GetStateChangeEvent(fidl_txn_t* txn);
    zx_status_t GetStateChangePort(fidl_txn_t* txn);
    zx_status_t SetTrip(uint32_t id, uint32_t temp, fidl_txn_t* txn);
    zx_status_t GetDvfsOperatingPoint(fuchsia_hardware_thermal_PowerDomain power_domain,
                                      fidl_txn_t* txn);
    zx_status_t SetDvfsOperatingPoint(uint16_t op_idx,
                                      fuchsia_hardware_thermal_PowerDomain power_domain,
                                      fidl_txn_t* txn);
    zx_status_t GetFanLevel(fidl_txn_t* txn);
    zx_status_t SetFanLevel(uint32_t fan_level, fidl_txn_t* txn);

    static constexpr fuchsia_hardware_thermal_Device_ops_t fidl_ops = {
        .GetInfo = fidl::Binder<AmlThermal>::BindMember<&AmlThermal::GetInfo>,
        .GetDeviceInfo = fidl::Binder<AmlThermal>::BindMember<&AmlThermal::GetDeviceInfo>,
        .GetDvfsInfo = fidl::Binder<AmlThermal>::BindMember<&AmlThermal::GetDvfsInfo>,
        .GetTemperature = fidl::Binder<AmlThermal>::BindMember<&AmlThermal::GetTemperature>,
        .GetStateChangeEvent =
            fidl::Binder<AmlThermal>::BindMember<&AmlThermal::GetStateChangeEvent>,
        .GetStateChangePort = fidl::Binder<AmlThermal>::BindMember<&AmlThermal::GetStateChangePort>,
        .SetTrip = fidl::Binder<AmlThermal>::BindMember<&AmlThermal::SetTrip>,
        .GetDvfsOperatingPoint =
            fidl::Binder<AmlThermal>::BindMember<&AmlThermal::GetDvfsOperatingPoint>,
        .SetDvfsOperatingPoint =
            fidl::Binder<AmlThermal>::BindMember<&AmlThermal::SetDvfsOperatingPoint>,
        .GetFanLevel = fidl::Binder<AmlThermal>::BindMember<&AmlThermal::GetFanLevel>,
        .SetFanLevel = fidl::Binder<AmlThermal>::BindMember<&AmlThermal::SetFanLevel>,
    };

    int ThermalNotificationThread();
    zx_status_t NotifyThermalDaemon();
    zx_status_t SetTarget(uint32_t opp_idx);

    fbl::unique_ptr<thermal::AmlTSensor> tsensor_;
    fbl::unique_ptr<thermal::AmlVoltageRegulator> voltage_regulator_;
    fbl::unique_ptr<thermal::AmlCpuFrequency> cpufreq_scaling_;
    opp_info_t opp_info_;
    fuchsia_hardware_thermal_ThermalDeviceInfo thermal_config_;
};
} // namespace thermal
