// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

#include <lib/mmio/mmio-buffer.h>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"

namespace i915_tgl {

namespace {

TEST(DdiBufferControlTest, DisplayPortLaneCount) {
  auto ddi_buf_ctl_a =
      tgl_registers::DdiBufferControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_A).FromValue(0);

  // The valid values and encodings are listed in the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 355
  // DG1: IHD-OS-DG1-Vol 2c-2.21 page 334
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 445
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 441

  ddi_buf_ctl_a.set_reg_value(0);
  ddi_buf_ctl_a.set_display_port_lane_count(1);
  EXPECT_EQ(0u, ddi_buf_ctl_a.display_port_lane_count_selection());
  EXPECT_EQ(1, ddi_buf_ctl_a.display_port_lane_count());

  ddi_buf_ctl_a.set_reg_value(0);
  ddi_buf_ctl_a.set_display_port_lane_count(2);
  EXPECT_EQ(1u, ddi_buf_ctl_a.display_port_lane_count_selection());
  EXPECT_EQ(2, ddi_buf_ctl_a.display_port_lane_count());

  ddi_buf_ctl_a.set_reg_value(0);
  ddi_buf_ctl_a.set_display_port_lane_count(4);
  EXPECT_EQ(3u, ddi_buf_ctl_a.display_port_lane_count_selection());
  EXPECT_EQ(4, ddi_buf_ctl_a.display_port_lane_count());
}

TEST(DdiBufferControlTest, GetForKabyLakeDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 442
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 438

  auto ddi_buf_ctl_a =
      tgl_registers::DdiBufferControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_A).FromValue(0);
  EXPECT_EQ(0x64000u, ddi_buf_ctl_a.reg_addr());

  auto ddi_buf_ctl_b =
      tgl_registers::DdiBufferControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_B).FromValue(0);
  EXPECT_EQ(0x64100u, ddi_buf_ctl_b.reg_addr());

  auto ddi_buf_ctl_c =
      tgl_registers::DdiBufferControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_C).FromValue(0);
  EXPECT_EQ(0x64200u, ddi_buf_ctl_c.reg_addr());

  auto ddi_buf_ctl_d =
      tgl_registers::DdiBufferControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_D).FromValue(0);
  EXPECT_EQ(0x64300u, ddi_buf_ctl_d.reg_addr());

  auto ddi_buf_ctl_e =
      tgl_registers::DdiBufferControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_E).FromValue(0);
  EXPECT_EQ(0x64400u, ddi_buf_ctl_e.reg_addr());
}

TEST(DdiBufferControlTest, GetForTigerLakeDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 pages 352-353
  // DG1: IHD-OS-DG1-Vol 2c-2.21 pages 331-332

  auto ddi_buf_ctl_a =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_A).FromValue(0);
  EXPECT_EQ(0x64000u, ddi_buf_ctl_a.reg_addr());

  auto ddi_buf_ctl_b =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_B).FromValue(0);
  EXPECT_EQ(0x64100u, ddi_buf_ctl_b.reg_addr());

  auto ddi_buf_ctl_c =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_C).FromValue(0);
  EXPECT_EQ(0x64200u, ddi_buf_ctl_c.reg_addr());

  auto ddi_buf_ctl_usbc1 =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_TC_1)
          .FromValue(0);
  EXPECT_EQ(0x64300u, ddi_buf_ctl_usbc1.reg_addr());

  auto ddi_buf_ctl_usbc2 =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_TC_2)
          .FromValue(0);
  EXPECT_EQ(0x64400u, ddi_buf_ctl_usbc2.reg_addr());

  auto ddi_buf_ctl_usbc3 =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_TC_3)
          .FromValue(0);
  EXPECT_EQ(0x64500u, ddi_buf_ctl_usbc3.reg_addr());

  auto ddi_buf_ctl_usbc4 =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_TC_4)
          .FromValue(0);
  EXPECT_EQ(0x64600u, ddi_buf_ctl_usbc4.reg_addr());

  auto ddi_buf_ctl_usbc5 =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_TC_5)
          .FromValue(0);
  EXPECT_EQ(0x64700u, ddi_buf_ctl_usbc5.reg_addr());

  auto ddi_buf_ctl_usbc6 =
      tgl_registers::DdiBufferControl::GetForTigerLakeDdi(tgl_registers::Ddi::DDI_TC_6)
          .FromValue(0);
  EXPECT_EQ(0x64800u, ddi_buf_ctl_usbc6.reg_addr());
}

TEST(DdiPhyConfigEntryTest, GetDdiInstance) {
  // The _0 register MMIO addresses come directly from the reference manuals.
  // They are the start of the address ranges for each DDI.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 446
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 442

  auto ddi_buf_trans_a_0_entry1 =
      tgl_registers::DdiPhyConfigEntry1::GetDdiInstance(tgl_registers::DDI_A, 0).FromValue(0);
  EXPECT_EQ(0x64e00u, ddi_buf_trans_a_0_entry1.reg_addr());

  auto ddi_buf_trans_b_0_entry1 =
      tgl_registers::DdiPhyConfigEntry1::GetDdiInstance(tgl_registers::DDI_B, 0).FromValue(0);
  EXPECT_EQ(0x64e60u, ddi_buf_trans_b_0_entry1.reg_addr());

  auto ddi_buf_trans_c_0_entry1 =
      tgl_registers::DdiPhyConfigEntry1::GetDdiInstance(tgl_registers::DDI_C, 0).FromValue(0);
  EXPECT_EQ(0x64ec0u, ddi_buf_trans_c_0_entry1.reg_addr());

  auto ddi_buf_trans_d_0_entry1 =
      tgl_registers::DdiPhyConfigEntry1::GetDdiInstance(tgl_registers::DDI_D, 0).FromValue(0);
  EXPECT_EQ(0x64f20u, ddi_buf_trans_d_0_entry1.reg_addr());

  auto ddi_buf_trans_e_0_entry1 =
      tgl_registers::DdiPhyConfigEntry1::GetDdiInstance(tgl_registers::DDI_E, 0).FromValue(0);
  EXPECT_EQ(0x64f80u, ddi_buf_trans_e_0_entry1.reg_addr());

  // The end of the address range for each DDI is the last (4th) byte of the
  // last (2nd) part of the last (9th) entry in the table.

  auto ddi_buf_trans_a_9_entry2 =
      tgl_registers::DdiPhyConfigEntry2::GetDdiInstance(tgl_registers::DDI_A, 9).FromValue(0);
  EXPECT_EQ(0x64e4fu, ddi_buf_trans_a_9_entry2.reg_addr() + 3);

  auto ddi_buf_trans_b_9_entry2 =
      tgl_registers::DdiPhyConfigEntry2::GetDdiInstance(tgl_registers::DDI_B, 9).FromValue(0);
  EXPECT_EQ(0x64eafu, ddi_buf_trans_b_9_entry2.reg_addr() + 3);

  auto ddi_buf_trans_c_9_entry2 =
      tgl_registers::DdiPhyConfigEntry2::GetDdiInstance(tgl_registers::DDI_C, 9).FromValue(0);
  EXPECT_EQ(0x64f0fu, ddi_buf_trans_c_9_entry2.reg_addr() + 3);

  auto ddi_buf_trans_d_9_entry2 =
      tgl_registers::DdiPhyConfigEntry2::GetDdiInstance(tgl_registers::DDI_D, 9).FromValue(0);
  EXPECT_EQ(0x64f6fu, ddi_buf_trans_d_9_entry2.reg_addr() + 3);

  auto ddi_buf_trans_e_9_entry2 =
      tgl_registers::DdiPhyConfigEntry2::GetDdiInstance(tgl_registers::DDI_E, 9).FromValue(0);
  EXPECT_EQ(0x64fcfu, ddi_buf_trans_e_9_entry2.reg_addr() + 3);
}

TEST(DdiPhyBalanceControlTest, BalanceLegSelectForDdi) {
  auto dispio_cr_tx_bmu_cr0 = tgl_registers::DdiPhyBalanceControl::Get().FromValue(0);

  dispio_cr_tx_bmu_cr0.set_reg_value(0);
  dispio_cr_tx_bmu_cr0.balance_leg_select_for_ddi(tgl_registers::DDI_A).set(5);
  EXPECT_EQ(5u, dispio_cr_tx_bmu_cr0.balance_leg_select_ddi_a());

  dispio_cr_tx_bmu_cr0.set_reg_value(0);
  dispio_cr_tx_bmu_cr0.balance_leg_select_for_ddi(tgl_registers::DDI_B).set(5);
  EXPECT_EQ(5u, dispio_cr_tx_bmu_cr0.balance_leg_select_ddi_b());

  dispio_cr_tx_bmu_cr0.set_reg_value(0);
  dispio_cr_tx_bmu_cr0.balance_leg_select_for_ddi(tgl_registers::DDI_C).set(5);
  EXPECT_EQ(5u, dispio_cr_tx_bmu_cr0.balance_leg_select_ddi_c());

  dispio_cr_tx_bmu_cr0.set_reg_value(0);
  dispio_cr_tx_bmu_cr0.balance_leg_select_for_ddi(tgl_registers::DDI_D).set(5);
  EXPECT_EQ(5u, dispio_cr_tx_bmu_cr0.balance_leg_select_ddi_d());

  dispio_cr_tx_bmu_cr0.set_reg_value(0);
  dispio_cr_tx_bmu_cr0.balance_leg_select_for_ddi(tgl_registers::DDI_E).set(5);
  EXPECT_EQ(5u, dispio_cr_tx_bmu_cr0.balance_leg_select_ddi_e());
}

TEST(DpTransportControlTest, GetForKabyLakeDdi) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 517-520
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 515-518

  auto dp_tp_ctl_a =
      tgl_registers::DpTransportControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_A).FromValue(0);
  EXPECT_EQ(0x64040u, dp_tp_ctl_a.reg_addr());

  auto dp_tp_ctl_b =
      tgl_registers::DpTransportControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_B).FromValue(0);
  EXPECT_EQ(0x64140u, dp_tp_ctl_b.reg_addr());

  auto dp_tp_ctl_c =
      tgl_registers::DpTransportControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_C).FromValue(0);
  EXPECT_EQ(0x64240u, dp_tp_ctl_c.reg_addr());

  auto dp_tp_ctl_d =
      tgl_registers::DpTransportControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_D).FromValue(0);
  EXPECT_EQ(0x64340u, dp_tp_ctl_d.reg_addr());

  auto dp_tp_ctl_e =
      tgl_registers::DpTransportControl::GetForKabyLakeDdi(tgl_registers::Ddi::DDI_E).FromValue(0);
  EXPECT_EQ(0x64440u, dp_tp_ctl_e.reg_addr());
}

TEST(DpTransportControlTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol2c-12.21 Part 1 pages 600-603
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 pages 572-575

  auto dp_tp_ctl_a =
      tgl_registers::DpTransportControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60540u, dp_tp_ctl_a.reg_addr());

  auto dp_tp_ctl_b =
      tgl_registers::DpTransportControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61540u, dp_tp_ctl_b.reg_addr());

  auto dp_tp_ctl_c =
      tgl_registers::DpTransportControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62540u, dp_tp_ctl_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x63540.
}

}  // namespace

}  // namespace i915_tgl
