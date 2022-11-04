// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/power.h"

#include <lib/mmio-ptr/fake.h>
#include <lib/mmio/mmio.h>
#include <zircon/pixelformat.h>

#include <memory>
#include <vector>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

class PowerTest : public ::testing::Test {
 public:
  PowerTest() = default;

  void SetUp() override {
    regs_.resize(kMinimumRegCount);
    reg_region_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.data(), sizeof(uint32_t),
                                                                kMinimumRegCount);
    mmio_buffer_.emplace(reg_region_->GetMmioBuffer());
  }

  void TearDown() override {}

 protected:
  constexpr static uint32_t kMinimumRegCount = 0x50000 / sizeof(uint32_t);
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> reg_region_;
  std::vector<ddk_fake::FakeMmioReg> regs_;
  std::optional<fdf::MmioBuffer> mmio_buffer_;
};

// Verify setting Power Well 2 status on Skylake platform.
TEST_F(PowerTest, Skylake_PowerWell2) {
  auto kPwrWellCtlIndex = tgl_registers::PowerWellControl::Get().addr() / sizeof(uint32_t);
  auto kFuseStatusIndex = tgl_registers::FuseStatus::Get().addr() / sizeof(uint32_t);

  bool pg2_status = false;

  regs_[kPwrWellCtlIndex].SetWriteCallback(
      [&pg2_status](uint64_t in) { pg2_status = in & (1 << 31); });

  regs_[kPwrWellCtlIndex].SetReadCallback([&pg2_status]() -> uint64_t { return pg2_status << 30; });

  regs_[kFuseStatusIndex].SetReadCallback([&pg2_status]() -> uint64_t { return pg2_status << 25; });

  constexpr uint16_t kDeviceIdSkylake = 0x191b;
  auto power = Power::New(&*mmio_buffer_, kDeviceIdSkylake);

  {
    auto power_well2_ref = PowerWellRef(power.get(), PowerWellId::PG2);
    EXPECT_TRUE(pg2_status);
  }
  EXPECT_FALSE(pg2_status);

  // Test resuming power well state.
  {
    auto power_well2_ref = PowerWellRef(power.get(), PowerWellId::PG2);
    EXPECT_TRUE(pg2_status);

    pg2_status = false;

    power->Resume();
    EXPECT_TRUE(pg2_status);
  }
  EXPECT_FALSE(pg2_status);

  // Test creating and deleting multiple power well refs to PG2.
  // PG2 should be disabled only after all power well refs are removed.
  constexpr size_t kNumRefs = 10;
  std::vector<PowerWellRef> refs(kNumRefs);
  for (size_t i = 0; i < kNumRefs; i++) {
    refs[i] = PowerWellRef(power.get(), PowerWellId::PG2);
  }

  for (size_t i = 0; i < kNumRefs; i++) {
    EXPECT_TRUE(pg2_status);
    refs[i] = {};
  }
  EXPECT_FALSE(pg2_status);
}

// Verify setting Misc / AUX IO status on Skylake platform.
TEST_F(PowerTest, Skylake_AuxIo) {
  constexpr uint16_t kDeviceIdSkylake = 0x191b;
  auto power = Power::New(&*mmio_buffer_, kDeviceIdSkylake);

  EXPECT_TRUE(power->GetAuxIoPowerState(DdiId::DDI_A));
  EXPECT_TRUE(power->GetAuxIoPowerState(DdiId::DDI_B));

  // Enable AUX IO.
  power->SetAuxIoPowerState(DdiId::DDI_A, true);
  power->SetAuxIoPowerState(DdiId::DDI_B, true);

  // On Skylake, the AUX IO power will be not disabled on-demand.
  power->SetAuxIoPowerState(DdiId::DDI_A, false);
  power->SetAuxIoPowerState(DdiId::DDI_B, false);
  EXPECT_TRUE(power->GetAuxIoPowerState(DdiId::DDI_A));
  EXPECT_TRUE(power->GetAuxIoPowerState(DdiId::DDI_B));
}

TEST_F(PowerTest, TigerLake_DdiIo) {
  const auto kPowerWellControlDdi2Index =
      tgl_registers::PowerWellControlDdi2::Get().addr() / sizeof(uint32_t);

  auto power_well_control_ddi_reg = tgl_registers::PowerWellControlDdi2::Get().FromValue(0);

  // Fake PowerWellControlDdi2 register, which flips the state bit once the
  // corresponding request bit is flipped.
  regs_[kPowerWellControlDdi2Index].SetWriteCallback([&power_well_control_ddi_reg](uint64_t in) {
    constexpr uint32_t kPowerStateBitMask = 0b010101010101010101;

    const uint32_t current_power_state_bits =
        power_well_control_ddi_reg.reg_value() & kPowerStateBitMask;
    const uint32_t incoming_power_state_bits = static_cast<uint32_t>(in) & kPowerStateBitMask;
    EXPECT_EQ(incoming_power_state_bits, current_power_state_bits)
        << "power state bits must not be modified";

    power_well_control_ddi_reg.set_reg_value(
        (power_well_control_ddi_reg.reg_value() & (~kPowerStateBitMask)) |
        ((in >> 1) & kPowerStateBitMask));
  });
  regs_[kPowerWellControlDdi2Index].SetReadCallback(
      [&power_well_control_ddi_reg]() { return power_well_control_ddi_reg.reg_value(); });

  constexpr uint16_t kDeviceIdTigerLake = 0x9a49;
  auto power = Power::New(&*mmio_buffer_, kDeviceIdTigerLake);

  // Enable DDI IO for DDI_A.
  EXPECT_FALSE(power->GetDdiIoPowerState(DdiId::DDI_A));
  power->SetDdiIoPowerState(DdiId::DDI_A, /*enable=*/true);
  // Power state should be changed for the fake MMIO.
  EXPECT_TRUE(power->GetDdiIoPowerState(DdiId::DDI_A));

  EXPECT_TRUE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_A).get());
  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_B).get());
  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_TC_1).get());

  // Disable DDI IO for DDI_A.
  power->SetDdiIoPowerState(DdiId::DDI_A, /*enable=*/false);
  EXPECT_FALSE(power->GetDdiIoPowerState(DdiId::DDI_A));

  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_A).get());
  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_B).get());
  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_TC_1).get());

  // Enable DDI IO for DDI_TC_1.
  EXPECT_FALSE(power->GetDdiIoPowerState(DdiId::DDI_TC_1));
  power->SetDdiIoPowerState(DdiId::DDI_TC_1, /*enable=*/true);
  // Power state should be changed for the fake MMIO.
  EXPECT_TRUE(power->GetDdiIoPowerState(DdiId::DDI_TC_1));

  EXPECT_TRUE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_TC_1).get());
  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_B).get());
  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_TC_6).get());

  // Disable DDI IO for DDI_TC_1.
  power->SetDdiIoPowerState(DdiId::DDI_TC_1, /*enable=*/false);
  EXPECT_FALSE(power->GetDdiIoPowerState(DdiId::DDI_TC_1));

  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_TC_1).get());
  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_B).get());
  EXPECT_FALSE(power_well_control_ddi_reg.ddi_io_power_state_tiger_lake(DdiId::DDI_TC_6).get());
}

// Verify setting Power Well status on Tiger lake platform.
TEST_F(PowerTest, TigerLake_PowerWell) {
  auto kPwrWellCtlIndex = tgl_registers::PowerWellControl::Get().addr() / sizeof(uint32_t);
  auto kFuseStatusIndex = tgl_registers::FuseStatus::Get().addr() / sizeof(uint32_t);

  std::unordered_map<PowerWellId, bool> pg_status = {
      {PowerWellId::PG1, true},  {PowerWellId::PG2, false}, {PowerWellId::PG3, false},
      {PowerWellId::PG4, false}, {PowerWellId::PG5, false},
  };
  uint64_t power_well_ctl_reg = 0u;

  regs_[kPwrWellCtlIndex].SetWriteCallback([&pg_status, &power_well_ctl_reg](uint64_t in) {
    pg_status[PowerWellId::PG2] = in & (1 << 3);
    pg_status[PowerWellId::PG3] = in & (1 << 5);
    pg_status[PowerWellId::PG4] = in & (1 << 7);
    pg_status[PowerWellId::PG5] = in & (1 << 9);

    power_well_ctl_reg = (pg_status[PowerWellId::PG1] << 0) | (in & (1 << 1)) |
                         (pg_status[PowerWellId::PG2] << 2) | (in & (1 << 3)) |
                         (pg_status[PowerWellId::PG3] << 4) | (in & (1 << 5)) |
                         (pg_status[PowerWellId::PG4] << 6) | (in & (1 << 7)) |
                         (pg_status[PowerWellId::PG5] << 8) | (in & (1 << 9));
  });

  regs_[kPwrWellCtlIndex].SetReadCallback(
      [&power_well_ctl_reg]() -> uint64_t { return power_well_ctl_reg; });

  regs_[kFuseStatusIndex].SetReadCallback([&pg_status]() -> uint64_t {
    return (pg_status[PowerWellId::PG1] << 26) | (pg_status[PowerWellId::PG2] << 25) |
           (pg_status[PowerWellId::PG3] << 24) | (pg_status[PowerWellId::PG4] << 23) |
           (pg_status[PowerWellId::PG5] << 22);
  });

  constexpr uint16_t kDeviceIdTigerLake = 0x9a49;
  auto power = Power::New(&*mmio_buffer_, kDeviceIdTigerLake);

  // When we enable a power well, all its dependencies will be enabled as well.
  {
    auto power_well5_ref = PowerWellRef(power.get(), PowerWellId::PG5);
    EXPECT_TRUE(pg_status[PowerWellId::PG1]);
    EXPECT_TRUE(pg_status[PowerWellId::PG2]);
    EXPECT_TRUE(pg_status[PowerWellId::PG3]);
    EXPECT_TRUE(pg_status[PowerWellId::PG4]);
    EXPECT_TRUE(pg_status[PowerWellId::PG5]);
  }
  // When the power well ref is removed, refcount of all dependencies will
  // decrease and power well will be automatically turned off if not used.
  EXPECT_FALSE(pg_status[PowerWellId::PG2]);
  EXPECT_FALSE(pg_status[PowerWellId::PG3]);
  EXPECT_FALSE(pg_status[PowerWellId::PG4]);
  EXPECT_FALSE(pg_status[PowerWellId::PG5]);

  // Verify that a power well is disabled only when *all* power well refs that
  // depends on that power well have been removed.
  {
    auto power_well5_ref = PowerWellRef(power.get(), PowerWellId::PG5);
    auto power_well3_ref = PowerWellRef(power.get(), PowerWellId::PG3);
    EXPECT_TRUE(pg_status[PowerWellId::PG1]);
    EXPECT_TRUE(pg_status[PowerWellId::PG2]);
    EXPECT_TRUE(pg_status[PowerWellId::PG3]);
    EXPECT_TRUE(pg_status[PowerWellId::PG4]);
    EXPECT_TRUE(pg_status[PowerWellId::PG5]);

    power_well5_ref = {};
    EXPECT_TRUE(pg_status[PowerWellId::PG2]);
    EXPECT_TRUE(pg_status[PowerWellId::PG3]);
    EXPECT_FALSE(pg_status[PowerWellId::PG4]);
    EXPECT_FALSE(pg_status[PowerWellId::PG5]);

    power_well3_ref = {};
    EXPECT_FALSE(pg_status[PowerWellId::PG2]);
    EXPECT_FALSE(pg_status[PowerWellId::PG3]);
    EXPECT_FALSE(pg_status[PowerWellId::PG4]);
    EXPECT_FALSE(pg_status[PowerWellId::PG5]);
  }

  // Test resuming power well state.
  {
    auto power_well4_ref = PowerWellRef(power.get(), PowerWellId::PG4);
    EXPECT_TRUE(pg_status[PowerWellId::PG2]);
    EXPECT_TRUE(pg_status[PowerWellId::PG3]);
    EXPECT_TRUE(pg_status[PowerWellId::PG4]);
    EXPECT_FALSE(pg_status[PowerWellId::PG5]);

    pg_status[PowerWellId::PG2] = pg_status[PowerWellId::PG3] = pg_status[PowerWellId::PG4] =
        pg_status[PowerWellId::PG5] = false;

    power->Resume();
    EXPECT_TRUE(pg_status[PowerWellId::PG2]);
    EXPECT_TRUE(pg_status[PowerWellId::PG3]);
    EXPECT_TRUE(pg_status[PowerWellId::PG4]);
    EXPECT_FALSE(pg_status[PowerWellId::PG5]);
  }
  EXPECT_FALSE(pg_status[PowerWellId::PG2]);
  EXPECT_FALSE(pg_status[PowerWellId::PG3]);
  EXPECT_FALSE(pg_status[PowerWellId::PG4]);
  EXPECT_FALSE(pg_status[PowerWellId::PG5]);
}

}  // namespace i915_tgl
