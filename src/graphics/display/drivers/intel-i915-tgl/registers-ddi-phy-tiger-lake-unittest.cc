// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi-phy-tiger-lake.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace tgl_registers {

namespace {

TEST(PhyMiscTest, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 664
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 663

  auto phy_misc_a = PhyMisc::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x64c00u, phy_misc_a.reg_addr());

  auto phy_misc_b = PhyMisc::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x64c04u, phy_misc_b.reg_addr());

  auto phy_misc_c = PhyMisc::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x64c08u, phy_misc_c.reg_addr());
}

TEST(PortCommonLane5Test, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 885
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page

  auto port_cl_dw5_a = PortCommonLane5::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162014u, port_cl_dw5_a.reg_addr());

  auto port_cl_dw5_b = PortCommonLane5::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c014u, port_cl_dw5_b.reg_addr());

  auto port_cl_dw5_c = PortCommonLane5::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160014u, port_cl_dw5_c.reg_addr());
}

TEST(PortCommonLaneMainLinkPowerTest, SetPoweredUpLanes) {
  auto port_cl_dw10 = PortCommonLaneMainLinkPower::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);

  // The test cases come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 888
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 900

  port_cl_dw10.set_reg_value(0).set_powered_up_lanes(4);
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane0());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane1());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane2());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane3());

  port_cl_dw10.set_reg_value(0).set_powered_up_lanes(2);
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane0());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane1());
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane2());
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane3());

  port_cl_dw10.set_reg_value(0).set_powered_up_lanes(1);
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane0());
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane1());
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane2());
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane3());
}

TEST(PortCommonLaneMainLinkPowerTest, SetPoweredUpLanesReversed) {
  auto port_cl_dw10 = PortCommonLaneMainLinkPower::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);

  // The test cases come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 888
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 900

  port_cl_dw10.set_reg_value(0).set_powered_up_lanes_reversed(4);
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane0());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane1());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane2());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane3());

  port_cl_dw10.set_reg_value(0).set_powered_up_lanes_reversed(2);
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane0());
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane1());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane2());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane3());

  port_cl_dw10.set_reg_value(0).set_powered_up_lanes_reversed(1);
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane0());
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane1());
  EXPECT_EQ(1u, port_cl_dw10.power_down_lane2());
  EXPECT_EQ(0u, port_cl_dw10.power_down_lane3());
}

TEST(PortCommonLaneMainLinkPowerTest, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 887
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 899

  auto port_cl_dw10_a = PortCommonLaneMainLinkPower::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162028u, port_cl_dw10_a.reg_addr());

  auto port_cl_dw10_b = PortCommonLaneMainLinkPower::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c028u, port_cl_dw10_b.reg_addr());

  auto port_cl_dw10_c = PortCommonLaneMainLinkPower::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160028u, port_cl_dw10_c.reg_addr());
}

TEST(PortCommonLaneMiscPowerTest, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 890
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 902

  auto port_cl_dw12_a = PortCommonLaneMiscPower::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162030u, port_cl_dw12_a.reg_addr());

  auto port_cl_dw12_b = PortCommonLaneMiscPower::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c030u, port_cl_dw12_b.reg_addr());

  auto port_cl_dw12_c = PortCommonLaneMiscPower::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160030u, port_cl_dw12_c.reg_addr());
}

TEST(PortCommonLanePowerStatusTest, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 892
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 904

  auto port_cl_dw15_a = PortCommonLanePowerStatus::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x16203cu, port_cl_dw15_a.reg_addr());

  auto port_cl_dw15_b = PortCommonLanePowerStatus::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c03cu, port_cl_dw15_b.reg_addr());

  auto port_cl_dw15_c = PortCommonLanePowerStatus::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x16003cu, port_cl_dw15_c.reg_addr());
}

TEST(PortCommonLane16Test, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 894
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 906

  auto port_cl_dw16_a = PortCommonLane16::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162040u, port_cl_dw16_a.reg_addr());

  auto port_cl_dw16_b = PortCommonLane16::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c040u, port_cl_dw16_b.reg_addr());

  auto port_cl_dw16_c = PortCommonLane16::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160040u, port_cl_dw16_c.reg_addr());
}

TEST(PortCompensation0Test, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 896
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 908

  auto port_comp_dw0_a = PortCompensation0::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162100u, port_comp_dw0_a.reg_addr());

  auto port_comp_dw0_b = PortCompensation0::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c100u, port_comp_dw0_b.reg_addr());

  auto port_comp_dw0_c = PortCompensation0::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160100u, port_comp_dw0_c.reg_addr());
}

TEST(PortCompensation1Test, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 897
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 909

  auto port_comp_dw1_a = PortCompensation1::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162104u, port_comp_dw1_a.reg_addr());

  auto port_comp_dw1_b = PortCompensation1::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c104u, port_comp_dw1_b.reg_addr());

  auto port_comp_dw1_c = PortCompensation1::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160104u, port_comp_dw1_c.reg_addr());
}

TEST(PortCompensationStatusTest, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 897
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 909

  auto port_comp_dw3_a = PortCompensationStatus::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x16210cu, port_comp_dw3_a.reg_addr());

  auto port_comp_dw3_b = PortCompensationStatus::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c10cu, port_comp_dw3_b.reg_addr());

  auto port_comp_dw3_c = PortCompensationStatus::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x16010cu, port_comp_dw3_c.reg_addr());
}

TEST(PortCompensationSourceTest, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 897
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 909

  auto port_comp_dw8_a = PortCompensationSource::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162120u, port_comp_dw8_a.reg_addr());

  auto port_comp_dw8_b = PortCompensationSource::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c120u, port_comp_dw8_b.reg_addr());

  auto port_comp_dw8_c = PortCompensationSource::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160120u, port_comp_dw8_c.reg_addr());
}

TEST(PortCompensationNominalVoltageReferencesTest, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 902
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 915

  auto port_comp_dw9_a =
      PortCompensationNominalVoltageReferences::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162124u, port_comp_dw9_a.reg_addr());

  auto port_comp_dw9_b =
      PortCompensationNominalVoltageReferences::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c124u, port_comp_dw9_b.reg_addr());

  auto port_comp_dw9_c =
      PortCompensationNominalVoltageReferences::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160124u, port_comp_dw9_c.reg_addr());
}

TEST(PortCompensationLowVoltageReferencesTest, GetForDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 903
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 916

  auto port_comp_dw10_a =
      PortCompensationLowVoltageReferences::GetForDdi(i915_tgl::DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x162128u, port_comp_dw10_a.reg_addr());

  auto port_comp_dw10_b =
      PortCompensationLowVoltageReferences::GetForDdi(i915_tgl::DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x6c128u, port_comp_dw10_b.reg_addr());

  auto port_comp_dw10_c =
      PortCompensationLowVoltageReferences::GetForDdi(i915_tgl::DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x160128u, port_comp_dw10_c.reg_addr());
}

struct LaneRegisterInstance {
  i915_tgl::DdiId ddi_id;
  PortLane lane;
  uint32_t address;
};

TEST(PortPhysicalCoding1, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 904-906
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 917-919

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x162304},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x162604},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x162804},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x162904},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a04},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b04},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c304},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c604},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c804},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c904},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca04},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb04},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x160304},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x160604},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x160804},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x160904},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a04},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b04},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_pcs_dw1 =
        PortPhysicalCoding1::GetForDdiLane(instance.ddi_id, instance.lane).FromValue(0);
    EXPECT_EQ(instance.address, port_pcs_dw1.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortPhysicalCoding9, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 908-910
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 922-924

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x162324},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x162624},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x162824},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x162924},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a24},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b24},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c324},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c624},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c824},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c924},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca24},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb24},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x160324},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x160624},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x160824},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x160924},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a24},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b24},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_pcs_dw9 =
        PortPhysicalCoding9::GetForDdiLane(instance.ddi_id, instance.lane).FromValue(0);
    EXPECT_EQ(instance.address, port_pcs_dw9.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortTransmitterMipiEqualizationTest, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 929-931
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 945-947

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x162380},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x162680},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x162880},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x162980},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a80},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b80},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c380},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c680},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c880},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c980},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca80},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb80},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x160380},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x160680},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x160880},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x160980},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a80},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b80},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_tx_dw0 =
        PortTransmitterMipiEqualization::GetForDdiLane(instance.ddi_id, instance.lane).FromValue(0);
    EXPECT_EQ(instance.address, port_tx_dw0.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortTransmitter1Test, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 932-934
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 949-951

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x162384},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x162684},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x162884},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x162984},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a84},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b84},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c384},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c684},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c884},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c984},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca84},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb84},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x160384},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x160684},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x160884},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x160984},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a84},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b84},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_tx_dw1 = PortTransmitter1::GetForDdiLane(instance.ddi_id, instance.lane).FromValue(0);
    EXPECT_EQ(instance.address, port_tx_dw1.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortTransmitterVoltageSwing, VoltageSwingSelect) {
  struct TestCase {
    int value;
    uint32_t bit3;
    uint32_t bits20;
  };
  static constexpr TestCase kTestCases[] = {
      {.value = 0b0000, .bit3 = 0, .bits20 = 0b000}, {.value = 0b0001, .bit3 = 0, .bits20 = 0b001},
      {.value = 0b0010, .bit3 = 0, .bits20 = 0b010}, {.value = 0b0100, .bit3 = 0, .bits20 = 0b100},
      {.value = 0b0101, .bit3 = 0, .bits20 = 0b101}, {.value = 0b0111, .bit3 = 0, .bits20 = 0b111},
      {.value = 0b1000, .bit3 = 1, .bits20 = 0b000}, {.value = 0b1001, .bit3 = 1, .bits20 = 0b001},
      {.value = 0b1010, .bit3 = 1, .bits20 = 0b010}, {.value = 0b1100, .bit3 = 1, .bits20 = 0b100},
      {.value = 0b1101, .bit3 = 1, .bits20 = 0b101}, {.value = 0b1111, .bit3 = 1, .bits20 = 0b111},
  };
  for (const TestCase& test_case : kTestCases) {
    SCOPED_TRACE(testing::Message() << "Value: " << test_case.value);
    auto port_tx_dw2 =
        PortTransmitterVoltageSwing::GetForDdiLane(i915_tgl::DdiId::DDI_A, PortLane::kMainLinkLane0)
            .FromValue(0);

    port_tx_dw2.set_reg_value(0).set_voltage_swing_select(test_case.value);
    EXPECT_EQ(test_case.bit3, port_tx_dw2.voltage_swing_select_bit3());
    EXPECT_EQ(test_case.bits20, port_tx_dw2.voltage_swing_select_bits20());
    EXPECT_EQ(test_case.value, port_tx_dw2.voltage_swing_select());
  }
}

TEST(PortTransmitterVoltageSwingTest, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 935-937
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 953-955

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x162388},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x162688},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x162888},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x162988},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a88},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b88},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c388},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c688},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c888},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c988},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca88},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb88},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x160388},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x160688},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x160888},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x160988},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a88},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b88},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_tx_dw2 =
        PortTransmitterVoltageSwing::GetForDdiLane(instance.ddi_id, instance.lane).FromValue(0);
    EXPECT_EQ(instance.address, port_tx_dw2.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortTransmitterEqualizationTest, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 938-940
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 957-959

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x162390},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x162690},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x162890},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x162990},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a90},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b90},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c390},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c690},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c890},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c990},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca90},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb90},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x160390},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x160690},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x160890},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x160990},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a90},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b90},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_tx_dw4 =
        PortTransmitterEqualization::GetForDdiLane(instance.ddi_id, instance.lane).FromValue(0);
    EXPECT_EQ(instance.address, port_tx_dw4.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortTransmitterVoltageTest, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 941-943
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 961-963

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x162394},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x162694},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x162894},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x162994},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a94},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b94},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c394},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c694},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c894},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c994},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca94},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb94},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x160394},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x160694},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x160894},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x160994},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a94},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b94},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_tx_dw5 =
        PortTransmitterVoltage::GetForDdiLane(instance.ddi_id, instance.lane).FromValue(0);
    EXPECT_EQ(instance.address, port_tx_dw5.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortTransmitterLowDropoutRegulatorTest, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 945-947
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 965-967

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x162398},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x162698},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x162898},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x162998},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a98},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b98},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c398},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c698},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c898},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c998},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca98},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb98},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x160398},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x160698},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x160898},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x160998},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a98},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b98},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_tx_dw6 =
        PortTransmitterLowDropoutRegulator::GetForDdiLane(instance.ddi_id, instance.lane)
            .FromValue(0);
    EXPECT_EQ(instance.address, port_tx_dw6.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortTransmitterNScalarTest, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 948-950
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 969-971

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x16239c},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x16269c},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x16289c},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x16299c},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162a9c},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162b9c},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c39c},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c69c},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c89c},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c99c},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6ca9c},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cb9c},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x16039c},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x16069c},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x16089c},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x16099c},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160a9c},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160b9c},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_tx_dw7 =
        PortTransmitterNScalar::GetForDdiLane(instance.ddi_id, instance.lane).FromValue(0);
    EXPECT_EQ(instance.address, port_tx_dw7.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

TEST(PortTransmitterDutyCycleCorrectionTest, GetForDdiLane) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 951-953
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 972-974

  static constexpr LaneRegisterInstance kInstances[] = {
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAux, .address = 0x1623a0},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kAll, .address = 0x1626a0},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane0, .address = 0x1628a0},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane1, .address = 0x1629a0},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane2, .address = 0x162aa0},
      {.ddi_id = i915_tgl::DdiId::DDI_A, .lane = PortLane::kMainLinkLane3, .address = 0x162ba0},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAux, .address = 0x6c3a0},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kAll, .address = 0x6c6a0},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane0, .address = 0x6c8a0},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane1, .address = 0x6c9a0},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane2, .address = 0x6caa0},
      {.ddi_id = i915_tgl::DdiId::DDI_B, .lane = PortLane::kMainLinkLane3, .address = 0x6cba0},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAux, .address = 0x1603a0},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kAll, .address = 0x1606a0},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane0, .address = 0x1608a0},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane1, .address = 0x1609a0},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane2, .address = 0x160aa0},
      {.ddi_id = i915_tgl::DdiId::DDI_C, .lane = PortLane::kMainLinkLane3, .address = 0x160ba0},
  };

  for (const LaneRegisterInstance& instance : kInstances) {
    auto port_tx_dw8 =
        PortTransmitterDutyCycleCorrection::GetForDdiLane(instance.ddi_id, instance.lane)
            .FromValue(0);
    EXPECT_EQ(instance.address, port_tx_dw8.reg_addr())
        << "DDI: " << static_cast<int>(instance.ddi_id)
        << " Lane: " << static_cast<int>(instance.lane);
  }
}

}  // namespace

}  // namespace tgl_registers
