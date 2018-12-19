// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/scpi.h>
#include <lib/sync/completion.h>
#include <lib/zx/port.h>
#include <threads.h>
#include <zircon/device/thermal.h>

#include <utility>

#pragma once

namespace thermal {

enum FanLevel {
    FAN_L0,
    FAN_L1,
    FAN_L2,
    FAN_L3,
};

class AmlThermal;
using DeviceType = ddk::Device<AmlThermal, ddk::Ioctlable, ddk::Unbindable>;

// AmlThermal implements the s912 AmLogic thermal driver.
class AmlThermal : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_THERMAL> {
public:
    AmlThermal(zx_device_t* device,
               const ddk::PDevProtocolProxy& pdev,
               const gpio_protocol_t& fan0_gpio_proto,
               const gpio_protocol_t& fan1_gpio_proto,
               const scpi_protocol_t& scpi_proto,
               const uint32_t& sensor_id,
               zx::port& port)
        : DeviceType(device),
          pdev_(pdev),
          fan0_gpio_(&fan0_gpio_proto),
          fan1_gpio_(&fan1_gpio_proto),
          scpi_(&scpi_proto),
          sensor_id_(sensor_id),
          port_(std::move(port)) {}

    // Create and bind a driver instance.
    static zx_status_t Create(zx_device_t* device);

    // Perform post-construction runtime initialization.
    zx_status_t Init();

    // Ddk-required methods.
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* actual);
    void DdkUnbind();
    void DdkRelease();

private:
    // Notification thread implementation.
    int Worker();

    // Set the fans to the given level.
    zx_status_t SetFanLevel(FanLevel level);

    // Notify the thermal daemon of the current settings.
    zx_status_t NotifyThermalDaemon(uint32_t trip_point) const;

    ddk::PDevProtocolProxy pdev_;
    ddk::GpioProtocolProxy fan0_gpio_;
    ddk::GpioProtocolProxy fan1_gpio_;
    ddk::ScpiProtocolProxy scpi_;

    uint32_t sensor_id_;
    zx::port port_;

    thrd_t worker_ = {};
    thermal_device_info_t info_ = {};
    FanLevel fan_level_ = FAN_L0;
    uint32_t temperature_ = 0;
    sync_completion quit_;
    uint32_t cur_bigcluster_opp_idx_ = 0;
    uint32_t cur_littlecluster_opp_idx_ = 0;
};

} // namespace thermal
