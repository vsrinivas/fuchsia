// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <lib/mmio/mmio.h>
#include <fbl/mutex.h>
#include <hwreg/mmio.h>

#include <optional>

namespace thermal {

// This class represents a generic PWM
// which provides interface to set the
// period and configure to appropriate
// duty cycle.
class AmlPwm {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlPwm);
    AmlPwm(uint32_t period, uint32_t hwpwm)
        : period_(period), hwpwm_(hwpwm){}
    zx_status_t Init(zx_device_t* parent);
    zx_status_t Configure(uint32_t duty_cycle);
    ~AmlPwm() = default;

private:
    uint32_t period_;
    uint32_t duty_cycle_;
    uint32_t hwpwm_;
    uint32_t pwm_duty_cycle_offset_;
    uint32_t enable_bit_;
    uint32_t clk_enable_bit_;
    pdev_protocol_t pdev_;
    std::optional<ddk::MmioBuffer> pwm_mmio_;
    fbl::Mutex pwm_lock_;
};
} // namespace thermal
