// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer.h"

#include <lib/mmio/mmio-buffer.h>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer-internal.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-typec.h"

namespace i915_tgl {

namespace {

const std::unordered_map<PowerWellId, PowerWellInfo> kPowerWellInfoTestDevice = {};

// A fake power well implementation used only for integration tests.
class TestPower : public Power {
 public:
  explicit TestPower(fdf::MmioBuffer* mmio_space) : Power(mmio_space, &kPowerWellInfoTestDevice) {}
  void Resume() override {}

  PowerWellRef GetCdClockPowerWellRef() override { return PowerWellRef(); }
  PowerWellRef GetPipePowerWellRef(PipeId pipe_id) override { return PowerWellRef(); }
  PowerWellRef GetDdiPowerWellRef(DdiId ddi_id) override { return PowerWellRef(); }

  bool GetDdiIoPowerState(DdiId ddi_id) override { return true; }
  void SetDdiIoPowerState(DdiId ddi_id, bool enable) override {}

  bool GetAuxIoPowerState(DdiId ddi_id) override {
    if (aux_state_.find(ddi_id) == aux_state_.end()) {
      aux_state_[ddi_id] = false;
    }
    return aux_state_[ddi_id];
  }

  void SetAuxIoPowerState(DdiId ddi_id, bool enable) override { aux_state_[ddi_id] = enable; }

 private:
  void SetPowerWell(PowerWellId power_well, bool enable) override {}

  std::unordered_map<DdiId, bool> aux_state_;
};

class TypeCDdiTigerLakeTest : public ::testing::Test {
 public:
  TypeCDdiTigerLakeTest() = default;

 protected:
  constexpr static int kMmioRangeSize = 0x200000;
  MockMmioRange mmio_range_{kMmioRangeSize, MockMmioRange::Size::k32};
  fdf::MmioBuffer mmio_buffer_{mmio_range_.GetMmioBuffer()};
  TestPower power_{nullptr};
};

constexpr uint32_t kMailboxInterfaceOffset = 0x138124;
constexpr uint32_t kMailboxData0Offset = 0x138128;
constexpr uint32_t kMailboxData1Offset = 0x13812c;

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_TypeCColdBlock_Success) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has exited TCCOLD.
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_, /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kUninitialized);
  EXPECT_TRUE(ddi.AdvanceEnableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
}

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_TypeCColdBlock_Failure) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  const size_t kMmioRegCount = kMmioRangeSize / sizeof(uint32_t);
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(kMmioRegCount);
  ddk_fake::FakeMmioRegRegion mmio_region(fake_mmio_regs.data(), sizeof(uint32_t), kMmioRegCount);
  fdf::MmioBuffer mmio_buffer = mmio_region.GetMmioBuffer();

  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback(
      [&](uint64_t value) { EXPECT_EQ(0x8000'0026, value) << "Unexpected command"; });
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetReadCallback(
      [&]() -> uint64_t { return 0x0000'0026; });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetReadCallback(
      [&]() -> uint64_t { return 0x0000'0001; });

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kUninitialized);
  EXPECT_FALSE(ddi.AdvanceEnableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
}

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_SafeModeSet_Success) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortPhyModeStatus
      // Type-C PHY is ready on DDI_TC_1.
      {.address = 0x163890, .value = 0x0000'0001},

      // DynamicFlexIoDisplayPortControllerSafeStateSettings
      // Disable safe mode.
      {.address = 0x163894, .value = 0x0000'0000},
      {.address = 0x163894, .value = 0x0000'0001, .write = true},
      {.address = 0x163894, .value = 0x0000'0001},

      // DynamicFlexIoScratchPad
      // Request Type-C live state.
      {.address = 0x1638A0, .value = 0x0000'103f},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
  EXPECT_TRUE(ddi.AdvanceEnableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
}

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_SafeModeSet_Failure) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortPhyModeStatus
      // Type-C PHY is not ready on DDI_TC_1.
      {.address = 0x163890, .value = 0x0000'0002},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
  EXPECT_FALSE(ddi.AdvanceEnableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
}

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_AuxPowerOn_Success) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  mmio_range_.Expect(MockMmioRange::AccessList({
      // HipIndexReg0
      {.address = 0x1010a0, .value = 0x0000'0000},
      {.address = 0x1010a0, .value = 0x0000'0002, .write = true},

      // DekelCommonConfigMicroControllerDword27
      // PHY uC firmware is ready.
      {.address = 0x16836C, .value = 0x0000'8000},

      // DdiAuxControl
      // Not using thunderbolt.
      {.address = 0x64310, .value = 0x0000'0800},
      {.address = 0x64310, .value = 0x0000'0000, .write = true},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
  EXPECT_TRUE(ddi.AdvanceEnableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kAuxPoweredOn);
}

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_AuxPowerOn_Failure) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  class TestPowerCannotEnableAux : public TestPower {
   public:
    explicit TestPowerCannotEnableAux(fdf::MmioBuffer* mmio_space) : TestPower(mmio_space) {}
    bool GetAuxIoPowerState(DdiId ddi_id) override { return false; }
  };
  TestPowerCannotEnableAux power_cannot_enable_aux(nullptr);

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_cannot_enable_aux, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
  EXPECT_FALSE(ddi.AdvanceEnableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kAuxPoweredOn);
}

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_Initialized_AlwaysSuccess) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kAuxPoweredOn);
  EXPECT_TRUE(ddi.AdvanceEnableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kInitialized);
}

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_Initialized_IsTerminal) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kInitialized);
  EXPECT_FALSE(ddi.AdvanceEnableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kInitialized);
}

TEST_F(TypeCDdiTigerLakeTest, DisableFsm_kInitialized_AlwaysSuccess) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kInitialized);
  EXPECT_TRUE(ddi.AdvanceDisableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kAuxPoweredOn);
}

TEST_F(TypeCDdiTigerLakeTest, DisableFsm_AuxPoweredOn_AlwaysSuccess) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  {
    class TestPowerCanDisableAux : public TestPower {
     public:
      explicit TestPowerCanDisableAux(fdf::MmioBuffer* mmio_space) : TestPower(mmio_space) {}
      bool GetAuxIoPowerState(DdiId ddi_id) override { return false; }
    };
    TestPowerCanDisableAux power_can_disable_aux(nullptr);

    TypeCDdiTigerLake ddi(kTargetDdiId, &power_can_disable_aux, &mmio_buffer_,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kAuxPoweredOn);
    EXPECT_TRUE(ddi.AdvanceDisableFsm());
    EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
              TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
  }

  {
    class TestPowerCannotDisableAux : public TestPower {
     public:
      explicit TestPowerCannotDisableAux(fdf::MmioBuffer* mmio_space) : TestPower(mmio_space) {}
      bool GetAuxIoPowerState(DdiId ddi_id) override { return true; }
    };
    TestPowerCannotDisableAux power_cannot_disable_aux(nullptr);

    TypeCDdiTigerLake ddi(kTargetDdiId, &power_cannot_disable_aux, &mmio_buffer_,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kAuxPoweredOn);
    EXPECT_TRUE(ddi.AdvanceDisableFsm());
    EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
              TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
  }
}

TEST_F(TypeCDdiTigerLakeTest, EnableFsm_SafeModeSet_AlwaysSuccess) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortControllerSafeStateSettings
      // Enable safe mode.
      {.address = 0x163894, .value = 0x0000'0001},
      {.address = 0x163894, .value = 0x0000'0000, .write = true},
      {.address = 0x163894, .value = 0x0000'0000},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
  EXPECT_TRUE(ddi.AdvanceDisableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
}

TEST_F(TypeCDdiTigerLakeTest, DisableFsm_TypeCColdUnblock_Success) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has re-entered TCCOLD.
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  {
    TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
    EXPECT_TRUE(ddi.AdvanceDisableFsm());
    EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
              TypeCDdiTigerLake::InitializationPhase::kUninitialized);
  }

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has not yet re-entered TCCOLD; other devices
      // may still using the Type-C.
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  {
    TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
    EXPECT_TRUE(ddi.AdvanceDisableFsm());
    EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
              TypeCDdiTigerLake::InitializationPhase::kUninitialized);
  }
}

TEST_F(TypeCDdiTigerLakeTest, DisableFsm_TypeCColdUnblock_Failure) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  const size_t kMmioRegCount = kMmioRangeSize / sizeof(uint32_t);
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(kMmioRegCount);
  ddk_fake::FakeMmioRegRegion mmio_region(fake_mmio_regs.data(), sizeof(uint32_t), kMmioRegCount);
  fdf::MmioBuffer mmio_buffer = mmio_region.GetMmioBuffer();

  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback(
      [&](uint64_t value) { EXPECT_EQ(0x8000'0026, value) << "Unexpected command"; });
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    // Always busy.
    return 0x8000'0026;
  });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetReadCallback(
      [&]() -> uint64_t { return 0x0000'0001; });

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
  EXPECT_FALSE(ddi.AdvanceDisableFsm());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
}

TEST_F(TypeCDdiTigerLakeTest, Enable_Idempotency) {
  TypeCDdiTigerLake ddi(DdiId::DDI_TC_1, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kInitialized);
  EXPECT_TRUE(ddi.Enable());
  EXPECT_TRUE(ddi.IsEnabled());

  EXPECT_TRUE(ddi.Enable());
  EXPECT_TRUE(ddi.IsEnabled());
}

TEST_F(TypeCDdiTigerLakeTest, Disable_Idempotency) {
  TypeCDdiTigerLake ddi(DdiId::DDI_TC_1, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kUninitialized);
  EXPECT_TRUE(ddi.Disable());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_TRUE(ddi.Disable());
  EXPECT_FALSE(ddi.IsEnabled());
}

TEST_F(TypeCDdiTigerLakeTest, Enable_OnlyValidOnHealthy) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  const size_t kMmioRegCount = kMmioRangeSize / sizeof(uint32_t);
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(kMmioRegCount);
  ddk_fake::FakeMmioRegRegion mmio_region(fake_mmio_regs.data(), sizeof(uint32_t), kMmioRegCount);
  fdf::MmioBuffer mmio_buffer = mmio_region.GetMmioBuffer();

  EXPECT_DEATH(
      {
        TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                              /*is_static_port=*/false);
        ddi.SetInitializationPhaseForTesting(
            TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
        ddi.Enable();
      },
      "IsHealthy");

  EXPECT_DEATH(
      {
        TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                              /*is_static_port=*/false);
        ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
        ddi.Enable();
      },
      "IsHealthy");

  EXPECT_DEATH(
      {
        TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                              /*is_static_port=*/false);
        ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kAuxPoweredOn);
        ddi.Enable();
      },
      "IsHealthy");

  EXPECT_NO_FATAL_FAILURE({
    TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kInitialized);
    ddi.Enable();
  });

  EXPECT_NO_FATAL_FAILURE({
    TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kUninitialized);
    ddi.Enable();
  });
}

TEST_F(TypeCDdiTigerLakeTest, Disable_FailsOnUnhealthy) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_1;

  const size_t kMmioRegCount = kMmioRangeSize / sizeof(uint32_t);
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(kMmioRegCount);
  ddk_fake::FakeMmioRegRegion mmio_region(fake_mmio_regs.data(), sizeof(uint32_t), kMmioRegCount);
  fdf::MmioBuffer mmio_buffer = mmio_region.GetMmioBuffer();

  {
    TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
    EXPECT_FALSE(ddi.Disable());
  }

  {
    TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kSafeModeSet);
    EXPECT_FALSE(ddi.Disable());
  }

  {
    TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                          /*is_static_port=*/false);
    ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kAuxPoweredOn);
    EXPECT_FALSE(ddi.Disable());
  }
}

TEST_F(TypeCDdiTigerLakeTest, Enable_Success) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_2;

  // Unblock TCCOLD state.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has exited TCCOLD.
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));

  // Set Type-C safe mode.
  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortPhyModeStatus
      // Type-C PHY is ready on DDI_TC_2.
      {.address = 0x163890, .value = 0x0000'0002},

      // DynamicFlexIoDisplayPortControllerSafeStateSettings
      // Disable safe mode.
      {.address = 0x163894, .value = 0x0000'0000},
      {.address = 0x163894, .value = 0x0000'0002, .write = true},
      {.address = 0x163894, .value = 0x0000'0002},

      // DynamicFlexIoScratchPad
      // Request Type-C live state.
      {.address = 0x1638A0, .value = 0x0000'3f10},
  }));

  // AUX power on
  mmio_range_.Expect(MockMmioRange::AccessList({
      // HipIndexReg0
      {.address = 0x1010a0, .value = 0x0000'0001},
      {.address = 0x1010a0, .value = 0x0000'0201, .write = true},

      // DekelCommonConfigMicroControllerDword27
      // PHY uC firmware is ready.
      {.address = 0x16936C, .value = 0x0000'8000},

      // DdiAuxControl
      // Not using thunderbolt.
      {.address = 0x64410, .value = 0x0000'0800},
      {.address = 0x64410, .value = 0x0000'0000, .write = true},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_TRUE(ddi.Enable());

  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_TRUE(ddi.IsEnabled());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kInitialized);
}

TEST_F(TypeCDdiTigerLakeTest, Enable_Failure_TcColdCannotBlock) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_2;

  bool tccold_unblock_requested = false;
  bool tccold_block_requested = false;

  const size_t kMmioRegCount = kMmioRangeSize / sizeof(uint32_t);
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(kMmioRegCount);
  ddk_fake::FakeMmioRegRegion mmio_region(fake_mmio_regs.data(), sizeof(uint32_t), kMmioRegCount);
  fdf::MmioBuffer mmio_buffer = mmio_region.GetMmioBuffer();

  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback(
      [&](uint64_t value) { EXPECT_EQ(0x8000'0026, value) << "Unexpected command"; });
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetReadCallback(
      [&]() -> uint64_t { return 0x0000'0026; });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetWriteCallback([&](uint32_t data) {
    if (data == 0x0000'0000) {
      // The driver makes TCCOLD block request first.
      EXPECT_FALSE(tccold_unblock_requested);
      tccold_block_requested = true;
    } else if (data == 0x0000'0001) {
      // After TCCOLD block request fails, it tries to revert the command and
      // unblocks TCCOLD.
      EXPECT_TRUE(tccold_block_requested);
      tccold_unblock_requested = true;
    } else {
      FAIL() << "Unexpected TCCOLD config";
    }
  });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    // TCCOLD block request never succeeds, the device is always in TCCOLD
    // state.
    return 0x0000'0001;
  });

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                        /*is_static_port=*/false);

  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_FALSE(ddi.Enable());

  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_EQ(ddi.GetPhysicalLayerInfo().connection_type, DdiPhysicalLayer::ConnectionType::kNone);
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kUninitialized);

  EXPECT_TRUE(tccold_block_requested);
  EXPECT_TRUE(tccold_unblock_requested);
}

TEST_F(TypeCDdiTigerLakeTest, Enable_Failure_SafeModePhyNotAvailable) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_2;

  // Unblock TCCOLD state.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has exited TCCOLD.
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));

  // Set Type-C safe mode.
  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortPhyModeStatus
      // Type-C PHY is not ready on DDI_TC_2.
      {.address = 0x163890, .value = 0x0000'0000},
  }));

  // Revert "Set Type-C safe mode".
  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortControllerSafeStateSettings
      // Enable safe mode.
      {.address = 0x163894, .value = 0x0000'0002},
      {.address = 0x163894, .value = 0x0000'0000, .write = true},
      {.address = 0x163894, .value = 0x0000'0000},
  }));

  // Revert "Unblock TCCOLD state".
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has re-entered TCCOLD.
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer_,
                        /*is_static_port=*/false);
  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_FALSE(ddi.Enable());

  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_EQ(ddi.GetPhysicalLayerInfo().connection_type, DdiPhysicalLayer::ConnectionType::kNone);
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kUninitialized);
}

TEST_F(TypeCDdiTigerLakeTest, Enable_Failure_CannotEnableAux) {
  static constexpr DdiId kTargetDdiId = DdiId::DDI_TC_2;

  class TestPowerCannotEnableAux : public TestPower {
   public:
    explicit TestPowerCannotEnableAux(fdf::MmioBuffer* mmio_space) : TestPower(mmio_space) {}
    void SetAuxIoPowerState(DdiId ddi_id, bool target_enabled) override {
      EXPECT_EQ(ddi_id, kTargetDdiId);
      (target_enabled ? enable_requested : disable_requested) = true;
    }
    bool GetAuxIoPowerState(DdiId ddi_id) override {
      EXPECT_EQ(ddi_id, kTargetDdiId);
      return false;
    }

    bool enable_requested = false;
    bool disable_requested = false;
  };
  TestPowerCannotEnableAux power_cannot_enable_aux(nullptr);

  // Unblock TCCOLD state.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has exited TCCOLD.
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));

  // Set Type-C safe mode.
  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortPhyModeStatus
      // Type-C PHY is ready on DDI_TC_2.
      {.address = 0x163890, .value = 0x0000'0002},

      // DynamicFlexIoDisplayPortControllerSafeStateSettings
      // Disable safe mode.
      {.address = 0x163894, .value = 0x0000'0000},
      {.address = 0x163894, .value = 0x0000'0002, .write = true},
      {.address = 0x163894, .value = 0x0000'0002},

      // DynamicFlexIoScratchPad
      // Request Type-C live state.
      {.address = 0x1638A0, .value = 0x0000'3f10},
  }));

  // AUX power on fails; AUX IO is powered off through `Power`.
  mmio_range_.Expect(MockMmioRange::AccessList{});

  // Revert "Set Type-C safe mode".
  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortControllerSafeStateSettings
      // Enable safe mode.
      {.address = 0x163894, .value = 0x0000'0002},
      {.address = 0x163894, .value = 0x0000'0000, .write = true},
      {.address = 0x163894, .value = 0x0000'0000},
  }));

  // Revert "Unblock TCCOLD state".
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has re-entered TCCOLD.
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_cannot_enable_aux, &mmio_buffer_,
                        /*is_static_port=*/false);
  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_FALSE(ddi.Enable());

  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_TRUE(power_cannot_enable_aux.enable_requested);
  EXPECT_TRUE(power_cannot_enable_aux.disable_requested);

  EXPECT_EQ(ddi.GetPhysicalLayerInfo().connection_type, DdiPhysicalLayer::ConnectionType::kNone);
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kUninitialized);
}

TEST_F(TypeCDdiTigerLakeTest, Enable_FailureOnBailout) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_2;

  const size_t kMmioRegCount = kMmioRangeSize / sizeof(uint32_t);
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(kMmioRegCount);
  ddk_fake::FakeMmioRegRegion mmio_region(fake_mmio_regs.data(), sizeof(uint32_t), kMmioRegCount);
  fdf::MmioBuffer mmio_buffer = mmio_region.GetMmioBuffer();

  bool most_recent_tccold_request_is_block = false;

  // TCCOLD command can be successfully handled only when it's a "Block" request
  // (i.e. on "Enable()").
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback(
      [&](uint64_t value) { EXPECT_EQ(0x8000'0026, value) << "Unexpected command"; });
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    return most_recent_tccold_request_is_block ? 0x0000'0026 : 0x8000'0026;
  });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetWriteCallback(
      [&](uint32_t data) { most_recent_tccold_request_is_block = (data == 0x0000'0000); });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetReadCallback(
      [&]() -> uint64_t { return 0x0000'0000; });

  // DynamicFlexIoDisplayPortPhyModeStatus: Type-C PHY is not ready on DDI_TC_2.
  const size_t phy_mode_status_reg_index =
      tgl_registers::DynamicFlexIoDisplayPortPhyModeStatus::GetForDdi(kTargetDdiId).addr() /
      sizeof(uint32_t);
  fake_mmio_regs[phy_mode_status_reg_index].SetReadCallback([] { return 0x0000'0000; });

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                        /*is_static_port=*/false);
  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_FALSE(ddi.Enable());

  EXPECT_FALSE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
}

TEST_F(TypeCDdiTigerLakeTest, Disable_Success) {
  static constexpr DdiId kTargetDdiId = DdiId::DDI_TC_2;

  class TestPowerTrackingAux : public TestPower {
   public:
    explicit TestPowerTrackingAux(fdf::MmioBuffer* mmio_space) : TestPower(mmio_space) {}
    void SetAuxIoPowerState(DdiId ddi_id, bool target_enabled) override {
      EXPECT_EQ(ddi_id, kTargetDdiId);
      EXPECT_FALSE(target_enabled);
      disable_requested = true;
    }
    bool GetAuxIoPowerState(DdiId ddi_id) override {
      EXPECT_EQ(ddi_id, kTargetDdiId);
      return !disable_requested;
    }

    bool disable_requested = false;
  };
  TestPowerTrackingAux power(nullptr);

  // AUX IO is powered off through `Power`.
  mmio_range_.Expect(MockMmioRange::AccessList{});

  // Revert "Set Type-C safe mode".
  mmio_range_.Expect(MockMmioRange::AccessList({
      // DynamicFlexIoDisplayPortControllerSafeStateSettings
      // Enable safe mode.
      {.address = 0x163894, .value = 0x0000'0002},
      {.address = 0x163894, .value = 0x0000'0000, .write = true},
      {.address = 0x163894, .value = 0x0000'0000},
  }));

  // Revert "Unblock TCCOLD state".
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      // The Type-C subsystem has re-entered TCCOLD.
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));

  TypeCDdiTigerLake ddi(kTargetDdiId, &power, &mmio_buffer_,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kInitialized);
  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_TRUE(ddi.IsEnabled());

  EXPECT_TRUE(ddi.Disable());

  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());

  EXPECT_TRUE(power.disable_requested);

  EXPECT_EQ(ddi.GetPhysicalLayerInfo().connection_type, DdiPhysicalLayer::ConnectionType::kNone);
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kUninitialized);
}

TEST_F(TypeCDdiTigerLakeTest, Disable_Failure_TcColdCannotUnblock) {
  constexpr DdiId kTargetDdiId = DdiId::DDI_TC_2;

  const size_t kMmioRegCount = kMmioRangeSize / sizeof(uint32_t);
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(kMmioRegCount);
  ddk_fake::FakeMmioRegRegion mmio_region(fake_mmio_regs.data(), sizeof(uint32_t), kMmioRegCount);
  fdf::MmioBuffer mmio_buffer = mmio_region.GetMmioBuffer();

  // Re-enable safe mode.
  const size_t safe_state_reg_index =
      tgl_registers::DynamicFlexIoDisplayPortControllerSafeStateSettings::GetForDdi(kTargetDdiId)
          .addr() /
      sizeof(uint32_t);
  bool safe_mode_enabled = false;
  fake_mmio_regs[safe_state_reg_index].SetWriteCallback([&](uint32_t value) {
    EXPECT_EQ(value, 0x0000'0000u);
    safe_mode_enabled = true;
  });
  fake_mmio_regs[safe_state_reg_index].SetReadCallback(
      [&] { return safe_mode_enabled ? 0x0000'0000 : 0x0000'0002; });

  // TCCOLD command fails on unblock.
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback(
      [&](uint64_t value) { EXPECT_EQ(0x8000'0026u, value) << "Unexpected command"; });
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetReadCallback(
      [&]() -> uint64_t { return 0x8000'0026; });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetWriteCallback(
      [&](uint64_t value) { EXPECT_EQ(0x0000'0001u, value) << "Unexpected TCCOLD state"; });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetReadCallback(
      [&]() -> uint64_t { return 0x0000'0001; });

  TypeCDdiTigerLake ddi(kTargetDdiId, &power_, &mmio_buffer,
                        /*is_static_port=*/false);
  ddi.SetInitializationPhaseForTesting(TypeCDdiTigerLake::InitializationPhase::kInitialized);
  EXPECT_TRUE(ddi.IsHealthy());
  EXPECT_TRUE(ddi.IsEnabled());

  EXPECT_FALSE(ddi.Disable());

  EXPECT_TRUE(safe_mode_enabled);

  EXPECT_FALSE(ddi.IsHealthy());
  EXPECT_FALSE(ddi.IsEnabled());
  EXPECT_EQ(ddi.GetInitializationPhaseForTesting(),
            TypeCDdiTigerLake::InitializationPhase::kTypeCColdBlocked);
}

TEST_F(TypeCDdiTigerLakeTest, ReadPhysicalLayerInfo_StaticPort) {
  for (const DdiId ddi_id : {
           DdiId::DDI_TC_1,
           DdiId::DDI_TC_2,
           DdiId::DDI_TC_3,
           DdiId::DDI_TC_4,
           DdiId::DDI_TC_5,
           DdiId::DDI_TC_6,
       }) {
    auto scratch_pad = tgl_registers::DynamicFlexIoScratchPad::GetForDdi(ddi_id).FromValue(0);
    scratch_pad.set_is_modular_flexi_io_adapter(true).set_firmware_supports_mfd(true);

    if ((ddi_id - DdiId::DDI_TC_1) % 2 == 0) {
      scratch_pad.set_type_c_live_state_connector_0(static_cast<uint32_t>(
          tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kNoHotplugDisplay));
    } else {
      scratch_pad.set_type_c_live_state_connector_1(static_cast<uint32_t>(
          tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kNoHotplugDisplay));
    }

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = scratch_pad.reg_addr(), .value = scratch_pad.reg_value()},
    }));

    const TypeCDdiTigerLake ddi(ddi_id, &power_, &mmio_buffer_,
                                /*is_static_port=*/true);
    const auto physical_layer_info = ddi.ReadPhysicalLayerInfo();
    EXPECT_EQ(physical_layer_info.ddi_type, TypeCDdiTigerLake::DdiType::kTypeC);
    EXPECT_EQ(physical_layer_info.connection_type, TypeCDdiTigerLake::ConnectionType::kBuiltIn);
    EXPECT_EQ(physical_layer_info.max_allowed_dp_lane_count, 4u);
  }
}

TEST_F(TypeCDdiTigerLakeTest, ReadPhysicalLayerInfo_NoTypeC) {
  for (const DdiId ddi_id : {
           DdiId::DDI_TC_1,
           DdiId::DDI_TC_2,
           DdiId::DDI_TC_3,
           DdiId::DDI_TC_4,
           DdiId::DDI_TC_5,
           DdiId::DDI_TC_6,
       }) {
    auto scratch_pad = tgl_registers::DynamicFlexIoScratchPad::GetForDdi(ddi_id).FromValue(0);
    scratch_pad.set_is_modular_flexi_io_adapter(true).set_firmware_supports_mfd(true);

    if ((ddi_id - DdiId::DDI_TC_1) % 2 == 0) {
      scratch_pad.set_type_c_live_state_connector_0(static_cast<uint32_t>(
          tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kNoHotplugDisplay));
    } else {
      scratch_pad.set_type_c_live_state_connector_1(static_cast<uint32_t>(
          tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kNoHotplugDisplay));
    }

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = scratch_pad.reg_addr(), .value = scratch_pad.reg_value()},
    }));

    const TypeCDdiTigerLake ddi(ddi_id, &power_, &mmio_buffer_,
                                /*is_static_port=*/false);
    const auto physical_layer_info = ddi.ReadPhysicalLayerInfo();
    EXPECT_EQ(physical_layer_info.ddi_type, TypeCDdiTigerLake::DdiType::kTypeC);
    EXPECT_EQ(physical_layer_info.connection_type, TypeCDdiTigerLake::ConnectionType::kNone);
    EXPECT_EQ(physical_layer_info.max_allowed_dp_lane_count, 0u);
  }
}

TEST_F(TypeCDdiTigerLakeTest, ReadPhysicalLayerInfo_TypeCDisplayPortAlt) {
  for (const DdiId ddi_id : {
           DdiId::DDI_TC_1,
           DdiId::DDI_TC_2,
           DdiId::DDI_TC_3,
           DdiId::DDI_TC_4,
           DdiId::DDI_TC_5,
           DdiId::DDI_TC_6,
       }) {
    auto scratch_pad = tgl_registers::DynamicFlexIoScratchPad::GetForDdi(ddi_id).FromValue(0);
    scratch_pad.set_is_modular_flexi_io_adapter(true).set_firmware_supports_mfd(true);

    if ((ddi_id - DdiId::DDI_TC_1) % 2 == 0) {
      scratch_pad.set_type_c_live_state_connector_0(static_cast<uint32_t>(
          tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kTypeCHotplugDisplay));
      scratch_pad.set_display_port_tx_lane_assignment_bits_connector_0(0b0011);
    } else {
      scratch_pad.set_type_c_live_state_connector_1(static_cast<uint32_t>(
          tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kTypeCHotplugDisplay));
      scratch_pad.set_display_port_tx_lane_assignment_bits_connector_1(0b0011);
    }

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = scratch_pad.reg_addr(), .value = scratch_pad.reg_value()},
    }));

    const TypeCDdiTigerLake ddi(ddi_id, &power_, &mmio_buffer_,
                                /*is_static_port=*/false);
    const auto physical_layer_info = ddi.ReadPhysicalLayerInfo();
    EXPECT_EQ(physical_layer_info.ddi_type, TypeCDdiTigerLake::DdiType::kTypeC);
    EXPECT_EQ(physical_layer_info.connection_type,
              TypeCDdiTigerLake::ConnectionType::kTypeCDisplayPortAltMode);
    EXPECT_EQ(physical_layer_info.max_allowed_dp_lane_count, 2u);
  }
}

TEST_F(TypeCDdiTigerLakeTest, ReadPhysicalLayerInfo_Thunderbolt) {
  for (const DdiId ddi_id : {
           DdiId::DDI_TC_1,
           DdiId::DDI_TC_2,
           DdiId::DDI_TC_3,
           DdiId::DDI_TC_4,
           DdiId::DDI_TC_5,
           DdiId::DDI_TC_6,
       }) {
    auto scratch_pad = tgl_registers::DynamicFlexIoScratchPad::GetForDdi(ddi_id).FromValue(0);
    scratch_pad.set_is_modular_flexi_io_adapter(true).set_firmware_supports_mfd(true);

    if ((ddi_id - DdiId::DDI_TC_1) % 2 == 0) {
      scratch_pad.set_type_c_live_state_connector_0(static_cast<uint32_t>(
          tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kThunderboltHotplugDisplay));
    } else {
      scratch_pad.set_type_c_live_state_connector_1(static_cast<uint32_t>(
          tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kThunderboltHotplugDisplay));
    }

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = scratch_pad.reg_addr(), .value = scratch_pad.reg_value()},
    }));

    const TypeCDdiTigerLake ddi(ddi_id, &power_, &mmio_buffer_,
                                /*is_static_port=*/false);
    const auto physical_layer_info = ddi.ReadPhysicalLayerInfo();
    EXPECT_EQ(physical_layer_info.ddi_type, TypeCDdiTigerLake::DdiType::kTypeC);
    EXPECT_EQ(physical_layer_info.connection_type,
              TypeCDdiTigerLake::ConnectionType::kTypeCThunderbolt);
    EXPECT_EQ(physical_layer_info.max_allowed_dp_lane_count, 4u);
  }
}

class ComboDdiTigerLakeTest : public ::testing::Test {
 public:
  ComboDdiTigerLakeTest() = default;
  ~ComboDdiTigerLakeTest() override = default;

  void SetUp() override {}
  void TearDown() override { mmio_range_.CheckAllAccessesReplayed(); }

 protected:
  constexpr static int kMmioRangeSize = 0x200000;
  MockMmioRange mmio_range_{kMmioRangeSize, MockMmioRange::Size::k32};
  fdf::MmioBuffer mmio_buffer_{mmio_range_.GetMmioBuffer()};
};

constexpr int kPhyMiscAOffset = 0x64c00;
constexpr int kPhyMiscBOffset = 0x64c04;

constexpr int kPortClDw5BOffset = 0x6c014;
constexpr int kPortCompDw0BOffset = 0x6c100;
constexpr int kPortCompDw1BOffset = 0x6c104;
constexpr int kPortCompDw3BOffset = 0x6c10c;
constexpr int kPortCompDw8BOffset = 0x6c120;
constexpr int kPortCompDw9BOffset = 0x6c124;
constexpr int kPortCompDw10BOffset = 0x6c128;
constexpr int kPortPcsDw1AuxBOffset = 0x6c304;
constexpr int kPortTxDw8AuxBOffset = 0x6c3a0;
constexpr int kPortPcsDw1Ln0BOffset = 0x6c804;
constexpr int kPortTxDw8Ln0BOffset = 0x6c8a0;
constexpr int kPortPcsDw1Ln1BOffset = 0x6c904;
constexpr int kPortTxDw8Ln1BOffset = 0x6c9a0;
constexpr int kPortPcsDw1Ln2BOffset = 0x6ca04;
constexpr int kPortTxDw8Ln2BOffset = 0x6caa0;
constexpr int kPortPcsDw1Ln3BOffset = 0x6cb04;
constexpr int kPortTxDw8Ln3BOffset = 0x6cba0;

constexpr int kPortClDw5AOffset = 0x162014;
constexpr int kPortCompDw0AOffset = 0x162100;
constexpr int kPortCompDw1AOffset = 0x162104;
constexpr int kPortCompDw3AOffset = 0x16210c;
constexpr int kPortCompDw8AOffset = 0x162120;
constexpr int kPortCompDw9AOffset = 0x162124;
constexpr int kPortCompDw10AOffset = 0x162128;
constexpr int kPortPcsDw1AuxAOffset = 0x162304;
constexpr int kPortTxDw8AuxAOffset = 0x1623a0;
constexpr int kPortPcsDw1Ln0AOffset = 0x162804;
constexpr int kPortTxDw8Ln0AOffset = 0x1628a0;
constexpr int kPortPcsDw1Ln1AOffset = 0x162904;
constexpr int kPortTxDw8Ln1AOffset = 0x1629a0;
constexpr int kPortPcsDw1Ln2AOffset = 0x162a04;
constexpr int kPortTxDw8Ln2AOffset = 0x162aa0;
constexpr int kPortPcsDw1Ln3AOffset = 0x162b04;
constexpr int kPortTxDw8Ln3AOffset = 0x162ba0;

TEST_F(ComboDdiTigerLakeTest, InitializeDdiADell5420) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPortCompDw3AOffset, .value = 0xc0606b25},
      {.address = kPortCompDw1AOffset, .value = 0x81000400},
      {.address = kPortCompDw9AOffset, .value = 0x62ab67bb},
      {.address = kPortCompDw10AOffset, .value = 0x51914f96},
      {.address = kPortClDw5AOffset, .value = 0x1204047b},
      {.address = kPortTxDw8AuxAOffset, .value = 0x30037c9c},
      {.address = kPortPcsDw1AuxAOffset, .value = 0x1c300004},
      {.address = kPortTxDw8Ln0AOffset, .value = 0x300335dc},
      {.address = kPortPcsDw1Ln0AOffset, .value = 0x1c300004},
      {.address = kPortTxDw8Ln1AOffset, .value = 0x3003379c},
      {.address = kPortPcsDw1Ln1AOffset, .value = 0x1c300004},
      {.address = kPortTxDw8Ln2AOffset, .value = 0x3003501c},
      {.address = kPortPcsDw1Ln2AOffset, .value = 0x1c300004},
      {.address = kPortTxDw8Ln3AOffset, .value = 0x3003501c},
      {.address = kPortPcsDw1Ln3AOffset, .value = 0x1c300004},
      {.address = kPhyMiscAOffset, .value = 0x23000000},
      {.address = kPortCompDw8AOffset, .value = 0x010d0280},
      {.address = kPortCompDw0AOffset, .value = 0x80005f25},
  }));

  ComboDdiTigerLake ddi(DdiId::DDI_A, &mmio_buffer_);
  EXPECT_EQ(true, ddi.Initialize());
}

TEST_F(ComboDdiTigerLakeTest, InitializeDdiBDell5420) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPortCompDw3BOffset, .value = 0xc0606b25},
      {.address = kPortCompDw1BOffset, .value = 0x81000400},
      {.address = kPortCompDw9BOffset, .value = 0x62ab67bb},
      {.address = kPortCompDw10BOffset, .value = 0x51914f96},
      {.address = kPortClDw5BOffset, .value = 0x12040478},
      {.address = kPortTxDw8AuxBOffset, .value = 0x3003501c},
      {.address = kPortPcsDw1AuxBOffset, .value = 0x1c300004},
      {.address = kPortTxDw8Ln0BOffset, .value = 0x3003501c},
      {.address = kPortPcsDw1Ln0BOffset, .value = 0x1c300004},
      {.address = kPortTxDw8Ln1BOffset, .value = 0x3003501c},
      {.address = kPortPcsDw1Ln1BOffset, .value = 0x1c300004},
      {.address = kPortTxDw8Ln2BOffset, .value = 0x3003501c},
      {.address = kPortPcsDw1Ln2BOffset, .value = 0x1c300004},
      {.address = kPortTxDw8Ln3BOffset, .value = 0x3003501c},
      {.address = kPortPcsDw1Ln3BOffset, .value = 0x1c300004},
      {.address = kPhyMiscBOffset, .value = 0x23000000},
      {.address = kPortCompDw8BOffset, .value = 0x000d0280},
      {.address = kPortCompDw0BOffset, .value = 0x80005f26},
  }));

  ComboDdiTigerLake ddi(DdiId::DDI_B, &mmio_buffer_);
  EXPECT_EQ(true, ddi.Initialize());
}

TEST_F(ComboDdiTigerLakeTest, InitializeDdiBNuc11) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPortCompDw3BOffset, .value = 0xc0608025},
      {.address = kPortCompDw1BOffset, .value = 0x81000400},
      {.address = kPortCompDw9BOffset, .value = 0x62ab67bb},
      {.address = kPortCompDw10BOffset, .value = 0x51914f96},
      {.address = kPortClDw5BOffset, .value = 0x1204047b},
      {.address = kPortTxDw8AuxBOffset, .value = 0x3003501c},
      {.address = kPortPcsDw1AuxBOffset, .value = 0x18300004},
      {.address = kPortTxDw8Ln0BOffset, .value = 0x300355fc},
      {.address = kPortPcsDw1Ln0BOffset, .value = 0x18300004},
      {.address = kPortTxDw8Ln1BOffset, .value = 0x300335fc},
      {.address = kPortPcsDw1Ln1BOffset, .value = 0x18300004},
      {.address = kPortTxDw8Ln2BOffset, .value = 0x300335bc},
      {.address = kPortPcsDw1Ln2BOffset, .value = 0x18300004},
      {.address = kPortTxDw8Ln3BOffset, .value = 0x300335dc},
      {.address = kPortPcsDw1Ln3BOffset, .value = 0x18300004},
      {.address = kPhyMiscBOffset, .value = 0x23000000},
      {.address = kPortCompDw8BOffset, .value = 0x000d0280},
      {.address = kPortCompDw0BOffset, .value = 0x80005f28},
  }));
  ComboDdiTigerLake ddi(DdiId::DDI_B, &mmio_buffer_);
  EXPECT_EQ(true, ddi.Initialize());
}

}  // namespace

}  // namespace i915_tgl
