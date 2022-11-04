// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/registers-typec.h"

#include <zircon/compiler.h>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"

namespace i915_tgl {

class RegisterTypeCTest : public ::testing::Test {
 protected:
  constexpr static int kMmioRangeSize = 0x100000;
  MockMmioRange mmio_range_{kMmioRangeSize, MockMmioRange::Size::k32};
  fdf::MmioBuffer mmio_buffer_{mmio_range_.GetMmioBuffer()};
};

using DynamicFlexIoDisplayPortMainLinkLaneEnabledTest = RegisterTypeCTest;

// TODO(fxbug.dev/110198): Separate MMIO address testing and bit helper methods
// testing.
TEST_F(DynamicFlexIoDisplayPortMainLinkLaneEnabledTest, Getter) {
  constexpr uint32_t kFiaOffsets[] = {0x1638C0, 0x16E8C0, 0x16F8C0};

  // Test `enabled_main_links_bits` getter.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kFiaOffsets[0], .value = 0x0000'003c, .write = false},
      {.address = kFiaOffsets[1], .value = 0x0000'00f3, .write = false},
      {.address = kFiaOffsets[2], .value = 0x0000'0031, .write = false},
  }));

  {
    const auto ddi_to_test = DdiId::DDI_TC_1;
    const auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_TC_1)
            .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.enabled_display_port_main_link_lane_bits(ddi_to_test), 0xcu);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_4;
    const auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_TC_4)
            .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.enabled_display_port_main_link_lane_bits(ddi_to_test), 0xfu);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_5;
    const auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_TC_5)
            .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.enabled_display_port_main_link_lane_bits(ddi_to_test), 0x1u);
  }
}

// TODO(fxbug.dev/110198): Separate MMIO address testing and bit helper methods
// testing.
TEST_F(DynamicFlexIoDisplayPortMainLinkLaneEnabledTest, Setter) {
  constexpr uint32_t kFiaOffsets[] = {0x1638C0, 0x16E8C0, 0x16F8C0};

  // Test `enabled_main_links_bits` getter.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kFiaOffsets[0], .value = 0x0000'003c, .write = false},
      {.address = kFiaOffsets[0], .value = 0x0000'003f, .write = true},
      {.address = kFiaOffsets[1], .value = 0x0000'00f3, .write = false},
      {.address = kFiaOffsets[1], .value = 0x0000'0013, .write = true},
      {.address = kFiaOffsets[2], .value = 0x0000'0031, .write = false},
      {.address = kFiaOffsets[2], .value = 0x0000'003c, .write = true},
  }));

  {
    const auto ddi_to_test = DdiId::DDI_TC_1;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(ddi_to_test)
            .ReadFrom(&mmio_buffer_);
    reg_to_test.set_enabled_display_port_main_link_lane_bits(ddi_to_test, 0xfu)
        .WriteTo(&mmio_buffer_);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_4;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(ddi_to_test)
            .ReadFrom(&mmio_buffer_);
    reg_to_test.set_enabled_display_port_main_link_lane_bits(ddi_to_test, 0x1u)
        .WriteTo(&mmio_buffer_);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_5;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(ddi_to_test)
            .ReadFrom(&mmio_buffer_);
    reg_to_test.set_enabled_display_port_main_link_lane_bits(ddi_to_test, 0xcu)
        .WriteTo(&mmio_buffer_);
  }
}

TEST_F(DynamicFlexIoDisplayPortMainLinkLaneEnabledTest, GetRejectsComboDdi) {
  // COMBO DDIs should not be supported
  EXPECT_DEATH(tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_A),
               "DDI_TC_1");
}

TEST_F(DynamicFlexIoDisplayPortMainLinkLaneEnabledTest,
       EnabledDisplayPortLaneBitsRejectsDdisFromDifferentFia) {
  EXPECT_DEATH(
      {
        const auto reg_to_test =
            tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_TC_1)
                .FromValue(0);
        reg_to_test.enabled_display_port_main_link_lane_bits(DdiId::DDI_B);
      },
      "IsDdiCoveredByThisRegister");

  // DDIs must be in the same FIA.
  EXPECT_DEATH(
      {
        const auto reg_to_test =
            tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_TC_1)
                .FromValue(0);
        reg_to_test.enabled_display_port_main_link_lane_bits(DdiId::DDI_TC_3);
      },
      "IsDdiCoveredByThisRegister");
}

TEST_F(DynamicFlexIoDisplayPortMainLinkLaneEnabledTest, SetterNotSupportComboDdi) {
  // COMBO DDIs should not be supported
  EXPECT_DEATH(
      {
        auto reg_to_test =
            tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_TC_1)
                .FromValue(0);
        reg_to_test.set_enabled_display_port_main_link_lane_bits(DdiId::DDI_B, 0x1u)
            .WriteTo(&mmio_buffer_);
      },
      "IsDdiCoveredByThisRegister");
}

TEST_F(DynamicFlexIoDisplayPortMainLinkLaneEnabledTest,
       SetEnabledDisplayPortLaneBitsRejectsDdisFromDifferentFia) {
  // DDIs must be in the same FIA.
  EXPECT_DEATH(
      {
        auto reg_to_test =
            tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_TC_1)
                .FromValue(0);
        reg_to_test.set_enabled_display_port_main_link_lane_bits(DdiId::DDI_TC_3, 0x3u)
            .WriteTo(&mmio_buffer_);
      },
      "IsDdiCoveredByThisRegister");

  // enabled_main_links_mask must be valid
  EXPECT_DEATH(
      tgl_registers::DynamicFlexIoDisplayPortMainLinkLaneEnabled::GetForDdi(DdiId::DDI_TC_1)
          .FromValue(0)
          .set_enabled_display_port_main_link_lane_bits(DdiId::DDI_TC_1, 0xau)
          .WriteTo(&mmio_buffer_);
      , "invalid");
}

using DynamicFlexIoScratchPadTest = RegisterTypeCTest;

// TODO(fxbug.dev/110198): Separate MMIO address testing and bit helper methods
// testing.
TEST_F(DynamicFlexIoScratchPadTest, Getter) {
  constexpr uint32_t kFiaOffsets[] = {0x1638A0, 0x16E8A0, 0x16F8A0};

  // Test `display_port_tx_lane_assignment`, `type_c_live_state` getters
  // and helper method `display_port_assigned_tx_lane_count`.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kFiaOffsets[0], .value = 0b0000'0000'0010'1100, .write = false},
      {.address = kFiaOffsets[1], .value = 0b0100'0001'0000'1111, .write = false},
      {.address = kFiaOffsets[2], .value = 0b0000'0000'0010'1111, .write = false},
  }));

  {
    const auto ddi_to_test = DdiId::DDI_TC_1;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoScratchPad::GetForDdi(ddi_to_test).ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_assigned_tx_lane_count(ddi_to_test), 2u);
    EXPECT_EQ(reg_to_test.display_port_tx_lane_assignment(ddi_to_test), 0b1100u);
    EXPECT_EQ(reg_to_test.type_c_live_state(ddi_to_test),
              tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kTypeCHotplugDisplay);

    EXPECT_EQ(reg_to_test.display_port_tx_lane_assignment_bits_connector_0(), 0b1100u);
    EXPECT_EQ(reg_to_test.type_c_live_state_connector_0(), 0b001u);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_4;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoScratchPad::GetForDdi(ddi_to_test).ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_assigned_tx_lane_count(ddi_to_test), 1u);
    EXPECT_EQ(reg_to_test.display_port_tx_lane_assignment(ddi_to_test), 0b0001u);
    EXPECT_EQ(reg_to_test.type_c_live_state(ddi_to_test),
              tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kThunderboltHotplugDisplay);

    EXPECT_EQ(reg_to_test.display_port_tx_lane_assignment_bits_connector_1(), 0b0001u);
    EXPECT_EQ(reg_to_test.type_c_live_state_connector_1(), 0b010u);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_5;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoScratchPad::GetForDdi(ddi_to_test).ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_assigned_tx_lane_count(ddi_to_test), 4u);
    EXPECT_EQ(reg_to_test.display_port_tx_lane_assignment(ddi_to_test), 0b1111u);
    EXPECT_EQ(reg_to_test.type_c_live_state(ddi_to_test),
              tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState::kTypeCHotplugDisplay);

    EXPECT_EQ(reg_to_test.display_port_tx_lane_assignment_bits_connector_0(), 0b1111u);
    EXPECT_EQ(reg_to_test.type_c_live_state_connector_0(), 0b001u);
  }
}

TEST_F(DynamicFlexIoScratchPadTest, GetRejectsComboDdi) {
  // COMBO DDIs should not be supported
  EXPECT_DEATH(tgl_registers::DynamicFlexIoScratchPad::GetForDdi(DdiId::DDI_A), "DDI_TC_1");
}

TEST_F(DynamicFlexIoScratchPadTest, HelperMethodsRejectDdiFromDifferentFia) {
  // DDIs must be in the same FIA.
  EXPECT_DEATH(tgl_registers::DynamicFlexIoScratchPad::GetForDdi(DdiId::DDI_TC_1)
                   .FromValue(0)
                   .display_port_assigned_tx_lane_count(DdiId::DDI_TC_3),
               "IsDdiCoveredByThisRegister");
  EXPECT_DEATH(tgl_registers::DynamicFlexIoScratchPad::GetForDdi(DdiId::DDI_TC_1)
                   .FromValue(0)
                   .display_port_tx_lane_assignment(DdiId::DDI_TC_3),
               "IsDdiCoveredByThisRegister");
  EXPECT_DEATH(tgl_registers::DynamicFlexIoScratchPad::GetForDdi(DdiId::DDI_TC_1)
                   .FromValue(0)
                   .type_c_live_state(DdiId::DDI_TC_3),
               "IsDdiCoveredByThisRegister");
}

using DynamicFlexIoPinAssignmentTest = RegisterTypeCTest;

// TODO(fxbug.dev/110198): Separate MMIO address testing and bit helper methods
// testing.
TEST_F(DynamicFlexIoPinAssignmentTest, Getter) {
  constexpr uint32_t kFiaOffsets[] = {0x163880, 0x16E880, 0x16F880};

  // Test `pin_assignment_for_ddi` getter.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kFiaOffsets[0], .value = 0b0000'0000'0000'0110, .write = false},
      {.address = kFiaOffsets[1], .value = 0b0000'0000'0001'0101, .write = false},
      {.address = kFiaOffsets[2], .value = 0b0000'0000'0000'0100, .write = false},
  }));

  {
    const auto ddi_to_test = DdiId::DDI_TC_1;
    auto reg_to_test = tgl_registers::DynamicFlexIoDisplayPortPinAssignment::GetForDdi(ddi_to_test)
                           .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.pin_assignment_for_ddi(ddi_to_test),
              tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kF);
    EXPECT_EQ(reg_to_test.display_port_pin_assignment_connector_0(), 0b0110u);

    EXPECT_EQ(
        reg_to_test.set_display_port_pin_assignment_connector_0(0b0000).pin_assignment_for_ddi(
            DdiId::DDI_TC_1),
        tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kNone);
    EXPECT_EQ(
        reg_to_test.set_display_port_pin_assignment_connector_0(0b0001).pin_assignment_for_ddi(
            DdiId::DDI_TC_1),
        tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kA);
    EXPECT_EQ(
        reg_to_test.set_display_port_pin_assignment_connector_0(0b0010).pin_assignment_for_ddi(
            DdiId::DDI_TC_1),
        tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kB);
    EXPECT_EQ(
        reg_to_test.set_display_port_pin_assignment_connector_0(0b0011).pin_assignment_for_ddi(
            DdiId::DDI_TC_1),
        tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kC);
    EXPECT_EQ(
        reg_to_test.set_display_port_pin_assignment_connector_0(0b0100).pin_assignment_for_ddi(
            DdiId::DDI_TC_1),
        tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kD);
    EXPECT_EQ(
        reg_to_test.set_display_port_pin_assignment_connector_0(0b0101).pin_assignment_for_ddi(
            DdiId::DDI_TC_1),
        tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kE);
    EXPECT_EQ(
        reg_to_test.set_display_port_pin_assignment_connector_0(0b0110).pin_assignment_for_ddi(
            DdiId::DDI_TC_1),
        tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kF);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_4;
    auto reg_to_test = tgl_registers::DynamicFlexIoDisplayPortPinAssignment::GetForDdi(ddi_to_test)
                           .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.pin_assignment_for_ddi(ddi_to_test),
              tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kA);
    EXPECT_EQ(reg_to_test.display_port_pin_assignment_connector_1(), 0b0001u);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_5;
    auto reg_to_test = tgl_registers::DynamicFlexIoDisplayPortPinAssignment::GetForDdi(ddi_to_test)
                           .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.pin_assignment_for_ddi(ddi_to_test),
              tgl_registers::DynamicFlexIoDisplayPortPinAssignment::PinAssignment::kD);
    EXPECT_EQ(reg_to_test.display_port_pin_assignment_connector_0(), 0b0100u);
  }
}

TEST_F(DynamicFlexIoPinAssignmentTest, GetRejectsComboDdi) {
  // COMBO DDIs should not be supported
  EXPECT_DEATH(tgl_registers::DynamicFlexIoDisplayPortPinAssignment::GetForDdi(DdiId::DDI_A),
               "DDI_TC_1");
}

TEST_F(DynamicFlexIoPinAssignmentTest, PinAssignmentRejectDdiFromDifferentFia) {
  // DDIs must be in the same FIA.
  EXPECT_DEATH(tgl_registers::DynamicFlexIoDisplayPortPinAssignment::GetForDdi(DdiId::DDI_TC_1)
                   .FromValue(0)
                   .pin_assignment_for_ddi(DdiId::DDI_TC_3),
               "IsDdiCoveredByThisRegister");
}

using DynamicFlexIoDisplayPortControllerSafeStateSettingsTest = RegisterTypeCTest;

// TODO(fxbug.dev/110198): Separate MMIO address testing and bit helper methods
// testing.
TEST_F(DynamicFlexIoDisplayPortControllerSafeStateSettingsTest, Getter) {
  constexpr uint32_t kFiaOffsets[] = {0x163894, 0x16E894, 0x16F894};

  // Test `set_disable_safe_mode_for_ddi` setter.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kFiaOffsets[0], .value = 0b0000'0000'0000'0000, .write = false},
      {.address = kFiaOffsets[0], .value = 0b0000'0000'0000'0001, .write = true},
      {.address = kFiaOffsets[1], .value = 0b0000'0000'0000'0010, .write = false},
      {.address = kFiaOffsets[1], .value = 0b0000'0000'0000'0000, .write = true},
      {.address = kFiaOffsets[2], .value = 0b0000'0000'0000'0000, .write = false},
      {.address = kFiaOffsets[2], .value = 0b0000'0000'0000'0001, .write = true},
  }));

  {
    const auto ddi_to_test = DdiId::DDI_TC_1;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortControllerSafeStateSettings::GetForDdi(ddi_to_test)
            .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_safe_mode_disabled_connector_0(), 0u);
    reg_to_test.set_safe_mode_disabled_for_ddi(ddi_to_test, true);
    EXPECT_EQ(reg_to_test.display_port_safe_mode_disabled_connector_0(), 1u);
    reg_to_test.WriteTo(&mmio_buffer_);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_4;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortControllerSafeStateSettings::GetForDdi(ddi_to_test)
            .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_safe_mode_disabled_connector_1(), 1u);
    reg_to_test.set_safe_mode_disabled_for_ddi(ddi_to_test, false);
    EXPECT_EQ(reg_to_test.display_port_safe_mode_disabled_connector_1(), 0u);
    reg_to_test.WriteTo(&mmio_buffer_);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_5;
    auto reg_to_test =
        tgl_registers::DynamicFlexIoDisplayPortControllerSafeStateSettings::GetForDdi(ddi_to_test)
            .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_safe_mode_disabled_connector_0(), 0u);
    reg_to_test.set_safe_mode_disabled_for_ddi(ddi_to_test, true);
    EXPECT_EQ(reg_to_test.display_port_safe_mode_disabled_connector_0(), 1u);
    reg_to_test.WriteTo(&mmio_buffer_);
  }
}

TEST_F(DynamicFlexIoDisplayPortControllerSafeStateSettingsTest, GetRejectsComboDdi) {
  // COMBO DDIs should not be supported
  EXPECT_DEATH(
      tgl_registers::DynamicFlexIoDisplayPortControllerSafeStateSettings::GetForDdi(DdiId::DDI_A),
      "DDI_TC_1");
}

TEST_F(DynamicFlexIoDisplayPortControllerSafeStateSettingsTest,
       SetSafeModeDisabledRejectsDdiFromDifferentFia) {
  // DDIs must be in the same FIA.
  EXPECT_DEATH(
      tgl_registers::DynamicFlexIoDisplayPortControllerSafeStateSettings::GetForDdi(DdiId::DDI_TC_1)
          .FromValue(0)
          .set_safe_mode_disabled_for_ddi(DdiId::DDI_TC_3, true),
      "IsDdiCoveredByThisRegister");
}

using DynamicFlexIoDisplayPortPhyModeStatusTest = RegisterTypeCTest;

// TODO(fxbug.dev/110198): Separate MMIO address testing and bit helper methods
// testing.
TEST_F(DynamicFlexIoDisplayPortPhyModeStatusTest, Getter) {
  constexpr uint32_t kFiaOffsets[] = {0x163890, 0x16E890, 0x16F890};

  // Test `phy_is_ready_for_ddi` getter.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kFiaOffsets[0], .value = 0b0000'0000'0000'0001, .write = false},
      {.address = kFiaOffsets[1], .value = 0b0000'0000'0000'0001, .write = false},
      {.address = kFiaOffsets[2], .value = 0b0000'0000'0000'0010, .write = false},
  }));

  {
    const auto ddi_to_test = DdiId::DDI_TC_1;
    auto reg_to_test = tgl_registers::DynamicFlexIoDisplayPortPhyModeStatus::GetForDdi(ddi_to_test)
                           .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_phy_is_ready_connector_0(), 1u);
    EXPECT_EQ(reg_to_test.display_port_phy_is_ready_connector_1(), 0u);
    EXPECT_EQ(reg_to_test.phy_is_ready_for_ddi(ddi_to_test), 1u);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_4;
    auto reg_to_test = tgl_registers::DynamicFlexIoDisplayPortPhyModeStatus::GetForDdi(ddi_to_test)
                           .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_phy_is_ready_connector_0(), 1u);
    EXPECT_EQ(reg_to_test.display_port_phy_is_ready_connector_1(), 0u);
    EXPECT_EQ(reg_to_test.phy_is_ready_for_ddi(ddi_to_test), 0u);
  }

  {
    const auto ddi_to_test = DdiId::DDI_TC_5;
    auto reg_to_test = tgl_registers::DynamicFlexIoDisplayPortPhyModeStatus::GetForDdi(ddi_to_test)
                           .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(reg_to_test.display_port_phy_is_ready_connector_0(), 0u);
    EXPECT_EQ(reg_to_test.display_port_phy_is_ready_connector_1(), 1u);
    EXPECT_EQ(reg_to_test.phy_is_ready_for_ddi(ddi_to_test), 0u);
  }
}

TEST_F(DynamicFlexIoDisplayPortPhyModeStatusTest, GetRejectsComboDdi) {
  // COMBO DDIs should not be supported
  EXPECT_DEATH(tgl_registers::DynamicFlexIoDisplayPortPhyModeStatus::GetForDdi(DdiId::DDI_A),
               "DDI_TC_1");
}

TEST_F(DynamicFlexIoDisplayPortPhyModeStatusTest, PhyIsReadyRejectsDdiFromDifferentFia) {
  // DDIs must be in the same FIA.
  EXPECT_DEATH(tgl_registers::DynamicFlexIoDisplayPortPhyModeStatus::GetForDdi(DdiId::DDI_TC_1)
                   .FromValue(0)
                   .phy_is_ready_for_ddi(DdiId::DDI_TC_3),
               "IsDdiCoveredByThisRegister");
}

TEST_F(RegisterTypeCTest, ReadWriteDekelRegister) {
  // TypeC Port 1 (HIP_INDEX_REG0)
  {
    constexpr uint32_t kPhysicalInternalAddress = 0x2200;
    constexpr auto kDdiToTest = DdiId::DDI_TC_1;
    constexpr uint32_t kHipIndex0Addr = 0x1010a0;
    constexpr uint32_t kBaseAddr = 0x168000;
    constexpr uint32_t kRegMmioAddr = kBaseAddr | (kPhysicalInternalAddress & 0xfff);

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kHipIndex0Addr, .value = 0x000cc000f, .write = false},
        {.address = kHipIndex0Addr, .value = 0x000cc0002, .write = true},
        {.address = kRegMmioAddr, .value = 0xfeedcafe, .write = false},
        {.address = kHipIndex0Addr, .value = 0x000cc000b, .write = false},
        {.address = kHipIndex0Addr, .value = 0x000cc0002, .write = true},
        {.address = kRegMmioAddr, .value = 0xdeadbeef, .write = true},
    }));

    auto dkl_reg =
        tgl_registers::DekelOpaqueRegister<kPhysicalInternalAddress>::GetForDdi(kDdiToTest)
            .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(dkl_reg.reg_value(), 0xfeedcafeu);
    dkl_reg.set_reg_value(0xdeadbeef).WriteTo(&mmio_buffer_);
  }

  // TypeC Port 6 (HIP_INDEX_REG1)
  {
    constexpr uint32_t kPhysicalInternalAddress = 0x2200;
    constexpr auto kDdiToTest = DdiId::DDI_TC_6;
    constexpr uint32_t kHipIndex1Addr = 0x1010a4;
    constexpr uint32_t kBaseAddr = 0x16D000;
    constexpr uint32_t kRegMmioAddr = kBaseAddr | (kPhysicalInternalAddress & 0xfff);

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kHipIndex1Addr, .value = 0x00cc0f00, .write = false},
        {.address = kHipIndex1Addr, .value = 0x00cc0200, .write = true},
        {.address = kRegMmioAddr, .value = 0xfeedcafe, .write = false},
        {.address = kHipIndex1Addr, .value = 0x00cc0b00, .write = false},
        {.address = kHipIndex1Addr, .value = 0x00cc0200, .write = true},
        {.address = kRegMmioAddr, .value = 0xdeadbeef, .write = true},
    }));

    auto dkl_reg =
        tgl_registers::DekelOpaqueRegister<kPhysicalInternalAddress>::GetForDdi(kDdiToTest)
            .ReadFrom(&mmio_buffer_);
    EXPECT_EQ(dkl_reg.reg_value(), 0xfeedcafeu);
    dkl_reg.set_reg_value(0xdeadbeef).WriteTo(&mmio_buffer_);
  }
}

TEST_F(RegisterTypeCTest, DekelLaneRegister) {
  {
    // Lane 0
    constexpr uint32_t kPhysicalInternalAddress = 0x00A0;
    constexpr uint32_t kHipIndex0Addr = 0x1010a0;
    constexpr uint32_t kBaseAddr = 0x168000;
    constexpr uint32_t kRegMmioAddr = kBaseAddr | (kPhysicalInternalAddress & 0xfff);

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kHipIndex0Addr, .value = 0x00cc000f, .write = false},
        {.address = kHipIndex0Addr, .value = 0x00cc0000, .write = true},
        {.address = kRegMmioAddr, .value = 0x000000f0, .write = false},
    }));

    tgl_registers::DekelDisplayPortMode::GetForLaneDdi(0, DdiId::DDI_TC_1).ReadFrom(&mmio_buffer_);
  }

  {
    // Lane 1
    constexpr uint32_t kPhysicalInternalAddress = 0x10A0;
    constexpr uint32_t kHipIndex0Addr = 0x1010a0;
    constexpr uint32_t kBaseAddr = 0x168000;
    constexpr uint32_t kRegMmioAddr = kBaseAddr | (kPhysicalInternalAddress & 0xfff);

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kHipIndex0Addr, .value = 0x00cc000f, .write = false},
        {.address = kHipIndex0Addr, .value = 0x00cc0001, .write = true},
        {.address = kRegMmioAddr, .value = 0x000000f0, .write = false},
    }));

    tgl_registers::DekelDisplayPortMode::GetForLaneDdi(1, DdiId::DDI_TC_1).ReadFrom(&mmio_buffer_);
  }
}

}  // namespace i915_tgl
