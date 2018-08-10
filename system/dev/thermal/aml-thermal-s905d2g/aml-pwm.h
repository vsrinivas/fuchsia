// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>

namespace thermal {

class AmlPwm {
public:
    AmlPwm(uint32_t period, uint32_t hwpwm)
        : period_(period), hwpwm_(hwpwm){};
    zx_status_t Init(zx_device_t* parent);
    zx_status_t Configure(uint32_t duty_cycle);
    ~AmlPwm();

private:
    uint32_t period_;
    uint32_t duty_cycle_;
    uint32_t hwpwm_;
    uint32_t pwm_duty_cycle_offset_;
    uint32_t enable_bit_;
    uint32_t clk_enable_bit_;
    platform_device_protocol_t pdev_;
    io_buffer_t pwm_mmio_;
    fbl::unique_ptr<hwreg::RegisterIo> pwm_regs_;
    fbl::Mutex pwm_lock_;
};
} // namespace thermal
