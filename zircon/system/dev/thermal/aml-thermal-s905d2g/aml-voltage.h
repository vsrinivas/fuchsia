// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "aml-pwm.h"
#include <fbl/unique_ptr.h>
#include <soc/aml-common/aml-thermal.h>

namespace thermal {
// This class represents a voltage regulator
// on the Amlogic board which provides interface
// to set and get current voltage for the CPU.
class AmlVoltageRegulator {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlVoltageRegulator);
    AmlVoltageRegulator(){}
    zx_status_t Init(zx_device_t* parent, aml_opp_info_t* opp_info);
    uint32_t GetVoltage();
    zx_status_t SetVoltage(uint32_t microvolt);

private:
    fbl::unique_ptr<thermal::AmlPwm> pwm_;
    aml_opp_info_t opp_info_;
    int current_voltage_index_;
};
} // namespace thermal
