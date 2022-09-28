// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/registers-transcoder.h"

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace i915_tgl {

namespace {

TEST(TranscoderDataMTest, PayloadSize) {
  // The two mappings come from the "TU or VC payload Size" field description in
  // the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 328
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 427-428
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 423
  //
  auto data_m_a =
      tgl_registers::TranscoderDataM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  data_m_a.set_reg_value(0);
  data_m_a.set_payload_size(64);
  EXPECT_EQ(63u, data_m_a.payload_size_select());
  EXPECT_EQ(64, data_m_a.payload_size());

  data_m_a.set_reg_value(0);
  data_m_a.set_payload_size(63);
  EXPECT_EQ(62u, data_m_a.payload_size_select());
  EXPECT_EQ(63, data_m_a.payload_size());
}

TEST(TranscoderDataMTest, GetForKabyLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 427
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 422

  auto data_m_a =
      tgl_registers::TranscoderDataM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60030u, data_m_a.reg_addr());

  auto data_m_b =
      tgl_registers::TranscoderDataM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61030u, data_m_b.reg_addr());

  auto data_m_c =
      tgl_registers::TranscoderDataM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62030u, data_m_c.reg_addr());

  auto data_m_edp =
      tgl_registers::TranscoderDataM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_EDP)
          .FromValue(0);
  EXPECT_EQ(0x6f030u, data_m_edp.reg_addr());
}

TEST(TranscoderDataMTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 328

  auto data_m_a =
      tgl_registers::TranscoderDataM::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60030u, data_m_a.reg_addr());

  auto data_m_b =
      tgl_registers::TranscoderDataM::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61030u, data_m_b.reg_addr());

  auto data_m_c =
      tgl_registers::TranscoderDataM::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62030u, data_m_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x63030.
}

TEST(TranscoderDataNTest, GetForKabyLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 429
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 424

  auto data_n_a =
      tgl_registers::TranscoderDataN::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60034u, data_n_a.reg_addr());

  auto data_n_b =
      tgl_registers::TranscoderDataN::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61034u, data_n_b.reg_addr());

  auto data_n_c =
      tgl_registers::TranscoderDataN::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62034u, data_n_c.reg_addr());

  auto data_n_edp =
      tgl_registers::TranscoderDataN::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_EDP)
          .FromValue(0);
  EXPECT_EQ(0x6f034u, data_n_edp.reg_addr());
}

TEST(TranscoderDataNTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 330

  auto data_n_a =
      tgl_registers::TranscoderDataN::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60034u, data_n_a.reg_addr());

  auto data_n_b =
      tgl_registers::TranscoderDataN::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61034u, data_n_b.reg_addr());

  auto data_n_c =
      tgl_registers::TranscoderDataN::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62034u, data_n_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x63034.
}

TEST(TranscoderLinkMTest, GetForKabyLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 1123
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 1112

  auto link_m_a =
      tgl_registers::TranscoderLinkM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60040u, link_m_a.reg_addr());

  auto link_m_b =
      tgl_registers::TranscoderLinkM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61040u, link_m_b.reg_addr());

  auto link_m_c =
      tgl_registers::TranscoderLinkM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62040u, link_m_c.reg_addr());

  auto link_m_edp =
      tgl_registers::TranscoderLinkM::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_EDP)
          .FromValue(0);
  EXPECT_EQ(0x6f040u, link_m_edp.reg_addr());
}

TEST(TranscoderLinkMTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1300

  auto link_m_a =
      tgl_registers::TranscoderLinkM::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60040u, link_m_a.reg_addr());

  auto link_m_b =
      tgl_registers::TranscoderLinkM::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61040u, link_m_b.reg_addr());

  auto link_m_c =
      tgl_registers::TranscoderLinkM::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62040u, link_m_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x63040.
}

TEST(TranscoderLinkNTest, GetForKabyLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 1124
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 1114

  auto link_n_a =
      tgl_registers::TranscoderLinkN::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60044u, link_n_a.reg_addr());

  auto link_n_b =
      tgl_registers::TranscoderLinkN::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61044u, link_n_b.reg_addr());

  auto link_n_c =
      tgl_registers::TranscoderLinkN::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62044u, link_n_c.reg_addr());

  auto link_n_edp =
      tgl_registers::TranscoderLinkN::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_EDP)
          .FromValue(0);
  EXPECT_EQ(0x6f044u, link_n_edp.reg_addr());
}

TEST(TranscoderLinkNTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1301

  auto link_n_a =
      tgl_registers::TranscoderLinkN::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60044u, link_n_a.reg_addr());

  auto link_n_b =
      tgl_registers::TranscoderLinkN::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61044u, link_n_b.reg_addr());

  auto link_n_c =
      tgl_registers::TranscoderLinkN::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62044u, link_n_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x63044.
}

}  // namespace

}  // namespace i915_tgl
