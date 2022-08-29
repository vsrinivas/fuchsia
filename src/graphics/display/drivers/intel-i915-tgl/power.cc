// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/power.h"

#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/time.h>

#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

namespace {

bool SetPowerWellImpl(const PowerWellInfo& power_well_info, bool enable,
                      const fdf::MmioBuffer* mmio_space, int32_t timeout_for_pwr_well_ctl_state_us,
                      int32_t timeout_for_fuse_state_us) {
  if (power_well_info.always_on) {
    return true;
  }

  // Sequences from IHD-OS-TGL-Vol 12-12.21 "Sequences for Power Wells".
  // "Enable sequence" on page 220, "Disable sequence" on page 221.

  auto power_well_reg = tgl_registers::PowerWellControl2::Get().ReadFrom(mmio_space);

  power_well_reg.power_request(power_well_info.request_bit_index).set(enable);
  power_well_reg.WriteTo(mmio_space);

  if (enable) {
    power_well_reg.ReadFrom(mmio_space);

    if (!PollUntil(
            [&] {
              return tgl_registers::PowerWellControl2::Get()
                  .ReadFrom(mmio_space)
                  .power_state(power_well_info.state_bit_index)
                  .get();
            },
            zx::usec(1), timeout_for_pwr_well_ctl_state_us)) {
      zxlogf(ERROR, "Failed to enable power well (%s)", power_well_info.name);
      return false;
    }

    if (!PollUntil(
            [&] {
              return tgl_registers::FuseStatus::Get()
                  .ReadFrom(mmio_space)
                  .dist_status(power_well_info.fuse_dist_bit_index);
            },
            zx::usec(1), timeout_for_fuse_state_us)) {
      zxlogf(ERROR, "Power well (%s) distribution failed", power_well_info.name);
      return false;
    }
  }

  return true;
}

const std::unordered_map<PowerWellId, PowerWellInfo> kTestPowerWellInfo = {
    {PowerWellId::PG1,
     {
         .name = "Power Well 1",
         .always_on = true,
         .state_bit_index = 0,
         .request_bit_index = 1,
         .fuse_dist_bit_index = 2,
         .parent = PowerWellId::PG1,
     }},
};

// A fake power well implementation used only for integration tests.
class TestPowerWell : public Power {
 public:
  explicit TestPowerWell(fdf::MmioBuffer* mmio_space) : Power(mmio_space, &kTestPowerWellInfo) {}
  void Resume() override {}

  PowerWellRef GetCdClockPowerWellRef() override { return PowerWellRef(this, PowerWellId::PG1); }
  PowerWellRef GetPipePowerWellRef(tgl_registers::Pipe pipe) override {
    return PowerWellRef(this, PowerWellId::PG1);
  }
  PowerWellRef GetDdiPowerWellRef(tgl_registers::Ddi ddi) override {
    return PowerWellRef(this, PowerWellId::PG1);
  }

  bool GetDdiIoPowerState(tgl_registers::Ddi ddi) override {
    if (ddi_state_.find(ddi) == ddi_state_.end()) {
      ddi_state_[ddi] = false;
    }
    return ddi_state_[ddi];
  }

  void SetDdiIoPowerState(tgl_registers::Ddi ddi, bool enable) override {
    ddi_state_[ddi] = enable;
  }

 private:
  void SetPowerWell(PowerWellId power_well, bool enable) override {}

  std::unordered_map<tgl_registers::Ddi, bool> ddi_state_;
};

const std::unordered_map<PowerWellId, PowerWellInfo> kSklPowerWellInfo = {
    {PowerWellId::PG1,
     {
         .name = "Power Well 1",
         .always_on = true,
         .state_bit_index = 28,
         .request_bit_index = 29,
         .fuse_dist_bit_index = 26,
         .parent = PowerWellId::PG1,
     }},
    {
        PowerWellId::PG2,
        {
            .name = "Power Well 2",
            .state_bit_index = 30,
            .request_bit_index = 31,
            .fuse_dist_bit_index = 25,
            .parent = PowerWellId::PG1,
        },
    },
};

// Power implementation for Sky lake and Kaby lake platforms.
class SklPower : public Power {
 public:
  explicit SklPower(fdf::MmioBuffer* mmio_space) : Power(mmio_space, &kSklPowerWellInfo) {}
  void Resume() override {
    if (ref_count().find(PowerWellId::PG2) != ref_count().end()) {
      SetPowerWell(PowerWellId::PG2, /* enable */ true);
    }
  }

  PowerWellRef GetCdClockPowerWellRef() override { return PowerWellRef(this, PowerWellId::PG1); }
  PowerWellRef GetPipePowerWellRef(tgl_registers::Pipe pipe) override {
    return PowerWellRef(this, pipe == tgl_registers::PIPE_A ? PowerWellId::PG1 : PowerWellId::PG2);
  }
  PowerWellRef GetDdiPowerWellRef(tgl_registers::Ddi ddi) override {
    return PowerWellRef(this, ddi == tgl_registers::DDI_A ? PowerWellId::PG1 : PowerWellId::PG2);
  }

  bool GetDdiIoPowerState(tgl_registers::Ddi ddi) override {
    auto power_well = tgl_registers::PowerWellControl2::Get().ReadFrom(mmio_space());
    return power_well.skl_ddi_io_power_state(ddi).get();
  }

  void SetDdiIoPowerState(tgl_registers::Ddi ddi, bool enable) override {
    auto power_well = tgl_registers::PowerWellControl2::Get().ReadFrom(mmio_space());
    power_well.skl_ddi_io_power_request(ddi).set(1);
    power_well.WriteTo(mmio_space());
  }

 private:
  void SetPowerWell(PowerWellId power_well, bool enable) override {
    const auto& power_well_info = power_well_info_map()->at(power_well);

    constexpr int32_t kWaitForPwrWellCtlStateUs = 20;
    constexpr int32_t kWaitForFuseStatusDistUs = 1;
    auto ok = SetPowerWellImpl(power_well_info, enable, mmio_space(), kWaitForPwrWellCtlStateUs,
                               kWaitForFuseStatusDistUs);

    ZX_DEBUG_ASSERT(ok);
  }
};

// Dependencies between power wells from IHD-OS-TGL-Vol 12-12.21
// "Enable Sequence", pages 220-221.
//
// FUSE_STATUS bits from IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 990-991.
// PWR_WELL_CTL bits from IHD-OS-TGL-Vol 2c-12.21 Part 2 pages 1063-1065.
const std::unordered_map<PowerWellId, PowerWellInfo> kTglPowerWellInfo = {
    // PG0 not tracked because it's managed by the CPU power controller.
    {PowerWellId::PG1,
     {
         .name = "Power Well 1",
         .always_on = true,
         .state_bit_index = 0,
         .request_bit_index = 1,
         .fuse_dist_bit_index = 26,
         .parent = PowerWellId::PG1,
     }},
    {PowerWellId::PG2,
     {
         .name = "Power Well 2",
         .state_bit_index = 2,
         .request_bit_index = 3,
         .fuse_dist_bit_index = 25,
         .parent = PowerWellId::PG1,
     }},
    {PowerWellId::PG3,
     {
         .name = "Power Well 3",
         .state_bit_index = 4,
         .request_bit_index = 5,
         .fuse_dist_bit_index = 24,
         .parent = PowerWellId::PG2,
     }},
    {PowerWellId::PG4,
     {
         .name = "Power Well 4",
         .state_bit_index = 6,
         .request_bit_index = 7,
         .fuse_dist_bit_index = 23,
         .parent = PowerWellId::PG3,
     }},
    {PowerWellId::PG5,
     {
         .name = "Power Well 5",
         .state_bit_index = 8,
         .request_bit_index = 9,
         .fuse_dist_bit_index = 22,
         .parent = PowerWellId::PG4,
     }},
};

// Power well implementation for Tiger lake platforms.
class TglPower : public Power {
 public:
  explicit TglPower(fdf::MmioBuffer* mmio_space) : Power(mmio_space, &kTglPowerWellInfo) {}
  void Resume() override {
    constexpr PowerWellId kPowerWellEnableSeq[] = {
        PowerWellId::PG2,
        PowerWellId::PG3,
        PowerWellId::PG4,
        PowerWellId::PG5,
    };
    for (const auto power_well : kPowerWellEnableSeq) {
      if (ref_count().find(power_well) != ref_count().end()) {
        SetPowerWell(power_well, /* enable */ true);
      }
    }
  }

  PowerWellRef GetCdClockPowerWellRef() override { return PowerWellRef(this, PowerWellId::PG1); }
  PowerWellRef GetPipePowerWellRef(tgl_registers::Pipe pipe) override {
    // Power well assignments from IHD-OS-TGL-Vol 12-12.21
    // "Functions Within Each Well", pages 219-220.

    // TODO(fxbug.dev/95863): Add all pipes supported by gen12.
    switch (pipe) {
      case tgl_registers::PIPE_A:
        return PowerWellRef(this, PowerWellId::PG1);
      case tgl_registers::PIPE_B:
        return PowerWellRef(this, PowerWellId::PG2);
      case tgl_registers::PIPE_C:
        return PowerWellRef(this, PowerWellId::PG3);
      case tgl_registers::PIPE_INVALID:
        ZX_ASSERT(false);
    }
  }

  PowerWellRef GetDdiPowerWellRef(tgl_registers::Ddi ddi) override {
    // Power well assignments from IHD-OS-TGL-Vol 12-12.21
    // "Functions Within Each Well", pages 219-220.

    switch (ddi) {
      case tgl_registers::DDI_A:
      case tgl_registers::DDI_B:
      case tgl_registers::DDI_C:
        return PowerWellRef(this, PowerWellId::PG1);
      case tgl_registers::DDI_TC_1:
      case tgl_registers::DDI_TC_2:
      case tgl_registers::DDI_TC_3:
      case tgl_registers::DDI_TC_4:
      case tgl_registers::DDI_TC_5:
      case tgl_registers::DDI_TC_6:
        return PowerWellRef(this, PowerWellId::PG3);
    }
  }

  bool GetDdiIoPowerState(tgl_registers::Ddi ddi) override {
    auto power_well = tgl_registers::PowerWellControlDdi2::Get().ReadFrom(mmio_space());
    return power_well.tgl_ddi_io_power_state(ddi).get();
  }

  void SetDdiIoPowerState(tgl_registers::Ddi ddi, bool enable) override {
    auto power_well = tgl_registers::PowerWellControlDdi2::Get().ReadFrom(mmio_space());
    power_well.tgl_ddi_io_power_state(ddi).set(1);
    power_well.WriteTo(mmio_space());
  }

 private:
  void SetPowerWell(PowerWellId power_well, bool enable) override {
    const auto& power_well_info = power_well_info_map()->at(power_well);

    constexpr int32_t kWaitForPwrWellCtlStateUs = 20;
    constexpr int32_t kWaitForFuseStatusDistUs = 20;
    auto ok = SetPowerWellImpl(power_well_info, enable, mmio_space(), kWaitForPwrWellCtlStateUs,
                               kWaitForFuseStatusDistUs);

    ZX_DEBUG_ASSERT(ok);
  }
};

}  // namespace

PowerWellRef::PowerWellRef() {}

PowerWellRef::PowerWellRef(Power* power, PowerWellId power_well)
    : power_(power), power_well_(power_well) {
  power_->IncRefCount(power_well);
}

PowerWellRef::PowerWellRef(PowerWellRef&& o) : power_(o.power_), power_well_(o.power_well_) {
  o.power_ = nullptr;
}

PowerWellRef& PowerWellRef::operator=(PowerWellRef&& o) {
  if (power_) {
    power_->DecRefCount(power_well_);
  }
  power_ = o.power_;
  power_well_ = o.power_well_;
  o.power_ = nullptr;
  return *this;
}

PowerWellRef::~PowerWellRef() {
  if (power_) {
    power_->DecRefCount(power_well_);
  }
}

Power::Power(fdf::MmioBuffer* mmio_space, const PowerWellInfoMap* power_well_info)
    : mmio_space_(mmio_space), power_well_info_map_(power_well_info) {}

void Power::IncRefCount(PowerWellId power_well) {
  ZX_DEBUG_ASSERT(power_well_info_map_ &&
                  power_well_info_map_->find(power_well) != power_well_info_map_->end());

  auto power_well_info = power_well_info_map_->at(power_well);
  auto parent = power_well_info.parent;
  if (parent != power_well) {
    IncRefCount(parent);
  }

  if (ref_count_.find(power_well) == ref_count_.end()) {
    if (!power_well_info.always_on) {
      SetPowerWell(power_well, /* enable */ true);
    }
    ref_count_[power_well] = 1;
  } else {
    ++ref_count_[power_well];
  }
}

void Power::DecRefCount(PowerWellId power_well) {
  ZX_DEBUG_ASSERT(power_well_info_map_ &&
                  power_well_info_map_->find(power_well) != power_well_info_map_->end());
  ZX_DEBUG_ASSERT(ref_count_.find(power_well) != ref_count_.end() && ref_count_.at(power_well) > 0);

  auto power_well_info = power_well_info_map_->at(power_well);
  if (ref_count_.at(power_well) == 1) {
    if (!power_well_info.always_on) {
      SetPowerWell(power_well, /* enable */ false);
    }
    ref_count_.erase(power_well);
  } else {
    --ref_count_[power_well];
  }

  auto parent = power_well_info.parent;
  if (parent != power_well) {
    DecRefCount(parent);
  }
}

// static
std::unique_ptr<Power> Power::New(fdf::MmioBuffer* mmio_space, uint16_t device_id) {
  if (is_skl(device_id) || is_kbl(device_id)) {
    return std::make_unique<SklPower>(mmio_space);
  }
  if (is_tgl(device_id)) {
    return std::make_unique<TglPower>(mmio_space);
  }
  if (is_test_device(device_id)) {
    return std::make_unique<TestPowerWell>(mmio_space);
  }
  ZX_DEBUG_ASSERT_MSG(false, "Device id %04x is not supported", device_id);
  return nullptr;
}

}  // namespace i915_tgl
