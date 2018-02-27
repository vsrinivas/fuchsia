// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "intel-i915.h"
#include "macros.h"
#include "power.h"
#include "registers.h"

namespace i915 {

PowerWellRef::PowerWellRef() {}

PowerWellRef::PowerWellRef(Power* power, PowerWell power_well)
        : power_(power), power_well_(power_well) {
    if (power_->power_well1_refs_++ == 0) {
        power_->SetPowerWell1Enable(true);
    }
    if (power_well_ == PowerWell2 && power_->power_well2_refs_++ == 0) {
        power_->SetPowerWell2Enable(true);
    }
}

PowerWellRef::PowerWellRef(PowerWellRef&& o) : power_(o.power_), power_well_(o.power_well_) {
    o.power_ = nullptr;
}

PowerWellRef& PowerWellRef::operator=(PowerWellRef&& o) {
    power_ = o.power_;
    power_well_ = o.power_well_;
    o.power_ = nullptr;
    return *this;
}

PowerWellRef::~PowerWellRef() {
    if (power_ == nullptr) {
        return;
    }
    if (power_well_ == PowerWell2 && --power_->power_well2_refs_ == 0) {
        power_->SetPowerWell2Enable(false);
    }
    if (--power_->power_well1_refs_ == 0) {
        power_->SetPowerWell1Enable(false);
    }
}

Power::Power(Controller* controller) : controller_(controller) {}

void Power::Resume() {
    if (power_well1_refs_ > 0) {
        SetPowerWell1Enable(true);
    }
    if (power_well2_refs_ > 0) {
        SetPowerWell2Enable(true);
    }
}

PowerWellRef Power::GetCdClockPowerWellRef() {
    return PowerWellRef(this, PowerWell1);
}

PowerWellRef Power::GetPipePowerWellRef(registers::Pipe pipe) {
    return PowerWellRef(this, pipe == registers::PIPE_A ? PowerWell1 : PowerWell2);
}

PowerWellRef Power::GetDdiPowerWellRef(registers::Ddi ddi) {
    return PowerWellRef(this, ddi == registers::DDI_A ? PowerWell1 : PowerWell2);
}

void Power::SetPowerWell1Enable(bool enable) {
    auto power_well = registers::PowerWellControl2::Get().ReadFrom(controller_->mmio_space());
    power_well.set_power_well_1_request(enable);
    power_well.set_misc_io_power_state(enable);
    power_well.WriteTo(controller_->mmio_space());

    if (enable) {
        if (!WAIT_ON_US(registers::PowerWellControl2
                ::Get().ReadFrom(controller_->mmio_space()).power_well_1_state(), 10)) {
            zxlogf(ERROR, "Power Well 1 failed to enable\n");
            return;
        }
        if (!WAIT_ON_US(registers::PowerWellControl2
                ::Get().ReadFrom(controller_->mmio_space()).misc_io_power_state(), 10)) {
            zxlogf(ERROR, "Misc IO power failed to enable\n");
            return;
        }
        if (!WAIT_ON_US(registers::FuseStatus
                ::Get().ReadFrom(controller_->mmio_space()).pg1_dist_status(), 5)) {
            zxlogf(ERROR, "Power Well 1 distribution failed\n");
            return;
        }
    } else {
        // Unconditionally sleep when disabling power well 1, as per the docs
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    }
}

void Power::SetPowerWell2Enable(bool enable) {
    auto power_well = registers::PowerWellControl2::Get().ReadFrom(controller_->mmio_space());
    power_well.set_power_well_2_request(enable);
    power_well.WriteTo(controller_->mmio_space());

    if (enable) {
        power_well.ReadFrom(controller_->mmio_space());
        if (!WAIT_ON_US(registers::PowerWellControl2
                ::Get().ReadFrom(controller_->mmio_space()).power_well_2_state(), 20)) {
            zxlogf(ERROR, "i915: failed to enable Power Well 2\n");
            return;
        }
        if (!WAIT_ON_US(registers::FuseStatus
                ::Get().ReadFrom(controller_->mmio_space()).pg2_dist_status(), 1)) {
            zxlogf(ERROR, "i915: Power Well 2 distribution failed\n");
            return;
        }
    }
}
} // namespace i915
