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

zx_status_t AmlThermal::SetTarget(uint32_t opp_idx) {
    if (opp_idx >= MAX_TRIP_POINTS) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Get current settings.
    uint32_t old_voltage = voltage_regulator_->GetVoltage();
    uint32_t old_frequency = cpufreq_scaling_->GetFrequency();

    // Get new settings.
    uint32_t new_voltage = opp_info_.opps[opp_idx].volt_mv;
    uint32_t new_frequency = opp_info_.opps[opp_idx].freq_hz;

    // If new settings are same as old, don't do anything.
    if (new_frequency == old_frequency) {
        return ZX_OK;
    }

    zx_status_t status;
    // Increasing CPU Frequency from current value, so we first change the voltage.
    if (new_frequency > old_frequency) {
        status = voltage_regulator_->SetVoltage(new_voltage);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml-thermal: Could not change CPU voltage: %d\n", status);
            return status;
        }
    }

    // Now let's change CPU frequency.
    status = cpufreq_scaling_->SetFrequency(new_frequency);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-thermal: Could not change CPU frequemcy: %d\n", status);
        // Failed to change CPU frequemcy, change back to old
        // voltage before returning.
        status = voltage_regulator_->SetVoltage(old_voltage);
        if (status != ZX_OK) {
            return status;
        }
        return status;
    }

    // Decreasing CPU Frequency from current value, changing voltage after frequency.
    if (new_frequency < old_frequency) {
        status = voltage_regulator_->SetVoltage(new_voltage);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml-thermal: Could not change CPU voltage: %d\n", status);
            return status;
        }
    }

    zxlogf(INFO, "Scaling from %d MHz, %u mV, --> %d MHz, %u mV\n",
           old_frequency / 1000000, old_voltage / 1000,
           new_frequency / 1000000, new_voltage / 1000);
    return ZX_OK;
}

int AmlThermal::ThermalNotificationThread() {
    zxlogf(INFO, "%s start\n", __func__);
    zx_status_t status;

    // Set the default CPU frequency
    uint32_t opp_idx = thermal_config_.trip_point_info[current_trip_idx_].big_cluster_dvfs_opp;
    status = SetTarget(opp_idx);

    if (status != ZX_OK) {
        return status;
    }

    while (running_.load()) {
        // TODO(braval): Implement the monitoring of temperature
        // and notifying thermal daemon about trip points
        // here.
    }
    return status;
}

zx_status_t AmlThermal::Create(zx_device_t* device) {
    fbl::AllocChecker ac;
    auto tsensor = fbl::make_unique_checked<thermal::AmlTSensor>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Initialize Temperature Sensor.
    zx_status_t status = tsensor->InitSensor(device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-thermal: Could not inititalize Temperature Sensor: %d\n", status);
        return status;
    }

    // Get the voltage-table & opp metadata.
    size_t actual;
    opp_info_t opp_info;
    status = device_get_metadata(device, VOLTAGE_DUTY_CYCLE_METADATA, &opp_info,
                                 sizeof(opp_info_), &actual);
    if (status != ZX_OK || actual != sizeof(opp_info_)) {
        zxlogf(ERROR, "aml-thermal: Could not get voltage-table metadata %d\n", status);
        return status;
    }

    // Get the thermal policy metadata.
    thermal_device_info_t thermal_config;
    status = device_get_metadata(device, THERMAL_CONFIG_METADATA, &thermal_config,
                                 sizeof(thermal_device_info_t), &actual);
    if (status != ZX_OK || actual != sizeof(thermal_device_info_t)) {
        zxlogf(ERROR, "aml-thermal: Could not get thermal config metadata %d\n", status);
        return status;
    }

    // Create the voltage regulator.
    auto voltage_regulator = fbl::make_unique_checked<thermal::AmlVoltageRegulator>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Initialize Temperature Sensor.
    status = voltage_regulator->Init(device, &opp_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-thermal: Could not inititalize Voltage Regulator: %d\n", status);
        return status;
    }

    // Create the CPU frequency scaling object.
    auto cpufreq_scaling = fbl::make_unique_checked<thermal::AmlCpuFrequency>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // Initialize CPU frequency scaling.
    status = cpufreq_scaling->Init(device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-thermal: Could not inititalize CPU freq. scaling: %d\n", status);
        return status;
    }

    auto thermal_device = fbl::make_unique_checked<thermal::
                                                       AmlThermal>(&ac, device,
                                                                   fbl::move(tsensor),
                                                                   fbl::move(voltage_regulator),
                                                                   fbl::move(cpufreq_scaling),
                                                                   fbl::move(opp_info),
                                                                   fbl::move(thermal_config));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = thermal_device->DdkAdd("thermal");
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-thermal: Could not create thermal device: %d\n", status);
        return status;
    }

    // Start thermal notification thread.
    auto start_thread = [](void* arg) -> int {
        return static_cast<AmlThermal*>(arg)->ThermalNotificationThread();
    };

    thermal_device->running_.store(true);
    int rc = thrd_create_with_name(&thermal_device->notification_thread_,
                                   start_thread,
                                   reinterpret_cast<void*>(thermal_device.get()),
                                   "aml_thermal_notify_thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
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
    running_.store(false);
    thrd_join(notification_thread_, NULL);
    DdkRemove();
}

void AmlThermal::DdkRelease() {
    delete this;
}

} // namespace thermal

extern "C" zx_status_t aml_thermal(void* ctx, zx_device_t* device) {
    return thermal::AmlThermal::Create(device);
}
