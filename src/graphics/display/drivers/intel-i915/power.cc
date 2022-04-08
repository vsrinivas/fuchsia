// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/power.h"

#include "src/graphics/display/drivers/intel-i915/intel-i915.h"
#include "src/graphics/display/drivers/intel-i915/macros.h"
#include "src/graphics/display/drivers/intel-i915/registers.h"

namespace i915 {

PowerWellRef::PowerWellRef() {}

PowerWellRef::PowerWellRef(Power* power, PowerWell power_well)
    : power_(power), power_well_(power_well) {
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
}

Power::Power(Controller* controller) : controller_(controller) {}

void Power::Resume() {
  if (power_well2_refs_ > 0) {
    SetPowerWell2Enable(true);
  }
}

PowerWellRef Power::GetCdClockPowerWellRef() { return PowerWellRef(this, PowerWell1); }

PowerWellRef Power::GetPipePowerWellRef(registers::Pipe pipe) {
  return PowerWellRef(this, pipe == registers::PIPE_A ? PowerWell1 : PowerWell2);
}

PowerWellRef Power::GetDdiPowerWellRef(registers::Ddi ddi) {
  return PowerWellRef(this, ddi == registers::DDI_A ? PowerWell1 : PowerWell2);
}

void Power::SetPowerWell2Enable(bool enable) {
  auto power_well = registers::PowerWellControl2::Get().ReadFrom(controller_->mmio_space());
  power_well.set_power_well_2_request(enable);
  power_well.WriteTo(controller_->mmio_space());

  if (enable) {
    power_well.ReadFrom(controller_->mmio_space());
    if (!WAIT_ON_US(registers::PowerWellControl2 ::Get()
                        .ReadFrom(controller_->mmio_space())
                        .power_well_2_state(),
                    20)) {
      zxlogf(ERROR, "Failed to enable Power Well 2");
      return;
    }
    if (!WAIT_ON_US(
            registers::FuseStatus ::Get().ReadFrom(controller_->mmio_space()).pg2_dist_status(),
            1)) {
      zxlogf(ERROR, "Power Well 2 distribution failed");
      return;
    }
  }
}
}  // namespace i915
