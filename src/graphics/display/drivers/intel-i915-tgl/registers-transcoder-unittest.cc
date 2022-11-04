// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/registers-transcoder.h"

#include <lib/mmio/mmio-buffer.h>

#include <optional>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace i915_tgl {

namespace {

TEST(TranscoderDdiControlTest, DdiKabyLake) {
  // The bit patterns come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 953
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 927

  auto transcoder_ddi_control_a =
      tgl_registers::TranscoderDdiControl::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_kaby_lake(std::nullopt);
  EXPECT_EQ(0b0'000'0000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::nullopt, transcoder_ddi_control_a.ddi_kaby_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_kaby_lake(DdiId::DDI_B);
  EXPECT_EQ(0b0'001'0000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_B), transcoder_ddi_control_a.ddi_kaby_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_kaby_lake(DdiId::DDI_C);
  EXPECT_EQ(0b0'010'0000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_C), transcoder_ddi_control_a.ddi_kaby_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_kaby_lake(DdiId::DDI_D);
  EXPECT_EQ(0b0'011'0000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_D), transcoder_ddi_control_a.ddi_kaby_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_kaby_lake(DdiId::DDI_E);
  EXPECT_EQ(0b0'100'0000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_E), transcoder_ddi_control_a.ddi_kaby_lake());
}

TEST(TranscoderDdiControlTest, DdiTigerLake) {
  // The bit patterns come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1371

  auto transcoder_ddi_control_a =
      tgl_registers::TranscoderDdiControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(std::nullopt);
  EXPECT_EQ(0b0'0000'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::nullopt, transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_A);
  EXPECT_EQ(0b0'0001'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_A), transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_B);
  EXPECT_EQ(0b0'0010'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_B), transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_C);
  EXPECT_EQ(0b0'0011'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_C), transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_TC_1);
  EXPECT_EQ(0b0'0100'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_1), transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_TC_2);
  EXPECT_EQ(0b0'0101'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_2), transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_TC_3);
  EXPECT_EQ(0b0'0110'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_3), transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_TC_4);
  EXPECT_EQ(0b0'0111'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_4), transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_TC_5);
  EXPECT_EQ(0b0'1000'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_5), transcoder_ddi_control_a.ddi_tiger_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_ddi_tiger_lake(DdiId::DDI_TC_6);
  EXPECT_EQ(0b0'1001'000'00000000'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_6), transcoder_ddi_control_a.ddi_tiger_lake());
}

TEST(TranscoderDdiControlTest, PortSyncPrimaryTranscoderKabyLake) {
  // The bit patterns come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 954
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 928

  auto transcoder_ddi_control_a =
      tgl_registers::TranscoderDdiControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_port_sync_primary_kaby_lake(tgl_registers::Trans::TRANS_EDP);
  EXPECT_EQ(0b00000000'0000'00'00'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(tgl_registers::Trans::TRANS_EDP,
            transcoder_ddi_control_a.port_sync_primary_transcoder_kaby_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_port_sync_primary_kaby_lake(tgl_registers::Trans::TRANS_A);
  EXPECT_EQ(0b00000000'0000'01'00'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(tgl_registers::Trans::TRANS_A,
            transcoder_ddi_control_a.port_sync_primary_transcoder_kaby_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_port_sync_primary_kaby_lake(tgl_registers::Trans::TRANS_B);
  EXPECT_EQ(0b00000000'0000'10'00'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(tgl_registers::Trans::TRANS_B,
            transcoder_ddi_control_a.port_sync_primary_transcoder_kaby_lake());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_port_sync_primary_kaby_lake(tgl_registers::Trans::TRANS_C);
  EXPECT_EQ(0b00000000'0000'11'00'00000000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(tgl_registers::Trans::TRANS_C,
            transcoder_ddi_control_a.port_sync_primary_transcoder_kaby_lake());
}

TEST(TranscoderDdiControlTest, InputPipe) {
  // The bit patterns come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1373
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 955

  auto transcoder_ddi_control_a =
      tgl_registers::TranscoderDdiControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_input_pipe(tgl_registers::Pipe::PIPE_A);
  EXPECT_EQ(0b00000000'00000000'0'000'0000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(tgl_registers::Pipe::PIPE_A, transcoder_ddi_control_a.input_pipe());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_input_pipe(tgl_registers::Pipe::PIPE_B);
  EXPECT_EQ(0b00000000'00000000'0'101'0000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(tgl_registers::Pipe::PIPE_B, transcoder_ddi_control_a.input_pipe());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_input_pipe(tgl_registers::Pipe::PIPE_C);
  EXPECT_EQ(0b00000000'00000000'0'110'0000'00000000u, transcoder_ddi_control_a.reg_value());
  EXPECT_EQ(tgl_registers::Pipe::PIPE_C, transcoder_ddi_control_a.input_pipe());

  // TODO(fxbug.dev/109278): Add a test for Tiger Lake's pipe D, when we support
  // it. The golden value is 0b00000000'00000000'0'111'0000'00000000u

  transcoder_ddi_control_a.set_reg_value(0b00000000'00000000'0'001'0000'00000000u);
  EXPECT_EQ(tgl_registers::Pipe::PIPE_INVALID, transcoder_ddi_control_a.input_pipe());

  transcoder_ddi_control_a.set_reg_value(0b00000000'00000000'0'010'0000'00000000u);
  EXPECT_EQ(tgl_registers::Pipe::PIPE_INVALID, transcoder_ddi_control_a.input_pipe());

  transcoder_ddi_control_a.set_reg_value(0b00000000'00000000'0'011'0000'00000000u);
  EXPECT_EQ(tgl_registers::Pipe::PIPE_INVALID, transcoder_ddi_control_a.input_pipe());
}

TEST(TranscoderDdiControlTest, DisplayPortLaneCount) {
  auto transcoder_ddi_control_a =
      tgl_registers::TranscoderDdiControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  // The valid values and encodings are listed in the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1374
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 956
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 930

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_display_port_lane_count(1);
  EXPECT_EQ(0u, transcoder_ddi_control_a.display_port_lane_count_selection());
  EXPECT_EQ(1, transcoder_ddi_control_a.display_port_lane_count());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_display_port_lane_count(2);
  EXPECT_EQ(1u, transcoder_ddi_control_a.display_port_lane_count_selection());
  EXPECT_EQ(2, transcoder_ddi_control_a.display_port_lane_count());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_display_port_lane_count(3);
  EXPECT_EQ(2u, transcoder_ddi_control_a.display_port_lane_count_selection());
  EXPECT_EQ(3, transcoder_ddi_control_a.display_port_lane_count());

  transcoder_ddi_control_a.set_reg_value(0);
  transcoder_ddi_control_a.set_display_port_lane_count(4);
  EXPECT_EQ(3u, transcoder_ddi_control_a.display_port_lane_count_selection());
  EXPECT_EQ(4, transcoder_ddi_control_a.display_port_lane_count());
}

TEST(TranscoderDdiControlTest, GetForKabyLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 952
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 926

  auto transcoder_ddi_control_a =
      tgl_registers::TranscoderDdiControl::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60400u, transcoder_ddi_control_a.reg_addr());

  auto transcoder_ddi_control_b =
      tgl_registers::TranscoderDdiControl::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61400u, transcoder_ddi_control_b.reg_addr());

  auto transcoder_ddi_control_c =
      tgl_registers::TranscoderDdiControl::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62400u, transcoder_ddi_control_c.reg_addr());

  auto transcoder_ddi_control_edp =
      tgl_registers::TranscoderDdiControl::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_EDP)
          .FromValue(0);
  EXPECT_EQ(0x6f400u, transcoder_ddi_control_edp.reg_addr());
}

TEST(TranscoderDdiControlTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1367-1368

  auto transcoder_ddi_control_a =
      tgl_registers::TranscoderDdiControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60400u, transcoder_ddi_control_a.reg_addr());

  auto transcoder_ddi_control_b =
      tgl_registers::TranscoderDdiControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61400u, transcoder_ddi_control_b.reg_addr());

  auto transcoder_ddi_control_c =
      tgl_registers::TranscoderDdiControl::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62400u, transcoder_ddi_control_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x63400.
}

TEST(TranscoderConfigTest, GetForKabyLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 949
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 924

  auto transcoder_config_a =
      tgl_registers::TranscoderConfig::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x70008u, transcoder_config_a.reg_addr());

  auto transcoder_config_b =
      tgl_registers::TranscoderConfig::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x71008u, transcoder_config_b.reg_addr());

  auto transcoder_config_c =
      tgl_registers::TranscoderConfig::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x72008u, transcoder_config_c.reg_addr());

  auto transcoder_config_edp =
      tgl_registers::TranscoderConfig::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_EDP)
          .FromValue(0);
  EXPECT_EQ(0x7f008u, transcoder_config_edp.reg_addr());

  // TODO(fxbug.com/109672): Add a test for the WD transcoder, when we support
  // it. The MMIO address is 0x7e008.
}

TEST(TranscoderConfigTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1367-1368

  auto transcoder_config_a =
      tgl_registers::TranscoderConfig::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x70008u, transcoder_config_a.reg_addr());

  auto transcoder_config_b =
      tgl_registers::TranscoderConfig::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x71008u, transcoder_config_b.reg_addr());

  auto transcoder_config_c =
      tgl_registers::TranscoderConfig::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x72008u, transcoder_config_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x73008.

  // TODO(fxbug.com/109672): Add a test for the WD transcoders, when we support
  // them. The MMIO addresses are 0x7e008 for WD0 and 0x7d008 for WD1.
}

TEST(TranscoderClockSelectTest, DdiClockKabyLake) {
  // The bit patterns come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 947
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 923

  auto transcoder_clock_select_a =
      tgl_registers::TranscoderClockSelect::GetForTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_kaby_lake(std::nullopt);
  EXPECT_EQ(0b000'00000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::nullopt, transcoder_clock_select_a.ddi_clock_kaby_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_kaby_lake(DdiId::DDI_B);
  EXPECT_EQ(0b010'00000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_B), transcoder_clock_select_a.ddi_clock_kaby_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_kaby_lake(DdiId::DDI_C);
  EXPECT_EQ(0b011'00000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_C), transcoder_clock_select_a.ddi_clock_kaby_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_kaby_lake(DdiId::DDI_D);
  EXPECT_EQ(0b100'00000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_D), transcoder_clock_select_a.ddi_clock_kaby_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_kaby_lake(DdiId::DDI_E);
  EXPECT_EQ(0b101'00000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_E), transcoder_clock_select_a.ddi_clock_kaby_lake());
}

TEST(TranscoderClockSelectTest, DdiClockKabyLakePreservesReservedBits) {
  auto transcoder_clock_select_a =
      tgl_registers::TranscoderClockSelect::GetForTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  transcoder_clock_select_a.set_reg_value(0xffff'ffff);
  transcoder_clock_select_a.set_ddi_clock_kaby_lake(std::nullopt);
  EXPECT_EQ(0b000'11111'11111111'11111111'11111111u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::nullopt, transcoder_clock_select_a.ddi_clock_kaby_lake());

  transcoder_clock_select_a.set_reg_value(0xffff'ffff);
  transcoder_clock_select_a.set_ddi_clock_kaby_lake(DdiId::DDI_D);
  EXPECT_EQ(0b100'11111'11111111'11111111'11111111u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_D), transcoder_clock_select_a.ddi_clock_kaby_lake());
}

TEST(TranscoderClockSelectTest, DdiClockTigerLake) {
  // The bit patterns come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1365

  auto transcoder_clock_select_a =
      tgl_registers::TranscoderClockSelect::GetForTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(std::nullopt);
  EXPECT_EQ(0b0000'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::nullopt, transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_A);
  EXPECT_EQ(0b0001'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_A), transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_B);
  EXPECT_EQ(0b0010'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_B), transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_C);
  EXPECT_EQ(0b0011'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_C), transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_TC_1);
  EXPECT_EQ(0b0100'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_1), transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_TC_2);
  EXPECT_EQ(0b0101'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_2), transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_TC_3);
  EXPECT_EQ(0b0110'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_3), transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_TC_4);
  EXPECT_EQ(0b0111'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_4), transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_TC_5);
  EXPECT_EQ(0b1000'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_5), transcoder_clock_select_a.ddi_clock_tiger_lake());

  transcoder_clock_select_a.set_reg_value(0);
  transcoder_clock_select_a.set_ddi_clock_tiger_lake(DdiId::DDI_TC_6);
  EXPECT_EQ(0b1001'0000'00000000'00000000'00000000u, transcoder_clock_select_a.reg_value());
  EXPECT_EQ(std::make_optional(DdiId::DDI_TC_6), transcoder_clock_select_a.ddi_clock_tiger_lake());
}

TEST(TranscoderClockSelectTest, GetForTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1365
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 947
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 922

  auto transcoder_clock_select_a =
      tgl_registers::TranscoderClockSelect::GetForTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x46140u, transcoder_clock_select_a.reg_addr());

  auto transcoder_clock_select_b =
      tgl_registers::TranscoderClockSelect::GetForTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x46144u, transcoder_clock_select_b.reg_addr());

  auto transcoder_clock_select_c =
      tgl_registers::TranscoderClockSelect::GetForTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x46148u, transcoder_clock_select_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x4614c.
}

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

TEST(TranscoderMainStreamAttributeMiscTest, GetForKabyLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 964
  // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 938

  auto transcoder_main_stream_attribute_misc_a =
      tgl_registers::TranscoderMainStreamAttributeMisc::GetForKabyLakeTranscoder(
          tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60410u, transcoder_main_stream_attribute_misc_a.reg_addr());

  auto transcoder_main_stream_attribute_misc_b =
      tgl_registers::TranscoderMainStreamAttributeMisc::GetForKabyLakeTranscoder(
          tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61410u, transcoder_main_stream_attribute_misc_b.reg_addr());

  auto transcoder_main_stream_attribute_misc_c =
      tgl_registers::TranscoderMainStreamAttributeMisc::GetForKabyLakeTranscoder(
          tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62410u, transcoder_main_stream_attribute_misc_c.reg_addr());

  auto transcoder_main_stream_attribute_misc_edp =
      tgl_registers::TranscoderMainStreamAttributeMisc::GetForKabyLakeTranscoder(
          tgl_registers::Trans::TRANS_EDP)
          .FromValue(0);
  EXPECT_EQ(0x6f410u, transcoder_main_stream_attribute_misc_edp.reg_addr());
}

TEST(TranscoderMainStreamAttributeMiscTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manuals.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1394

  auto transcoder_main_stream_attribute_misc_a =
      tgl_registers::TranscoderMainStreamAttributeMisc::GetForTigerLakeTranscoder(
          tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60410u, transcoder_main_stream_attribute_misc_a.reg_addr());

  auto transcoder_main_stream_attribute_misc_b =
      tgl_registers::TranscoderMainStreamAttributeMisc::GetForTigerLakeTranscoder(
          tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61410u, transcoder_main_stream_attribute_misc_b.reg_addr());

  auto transcoder_main_stream_attribute_misc_c =
      tgl_registers::TranscoderMainStreamAttributeMisc::GetForTigerLakeTranscoder(
          tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62410u, transcoder_main_stream_attribute_misc_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x63410.
}

TEST(TranscoderVariableRateRefreshControlTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manual.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1406

  auto transcoder_variable_rate_refresh_control_a =
      tgl_registers::TranscoderVariableRateRefreshControl::GetForTigerLakeTranscoder(
          tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x60420u, transcoder_variable_rate_refresh_control_a.reg_addr());

  auto transcoder_variable_rate_refresh_control_b =
      tgl_registers::TranscoderVariableRateRefreshControl::GetForTigerLakeTranscoder(
          tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x61420u, transcoder_variable_rate_refresh_control_b.reg_addr());

  auto transcoder_variable_rate_refresh_control_c =
      tgl_registers::TranscoderVariableRateRefreshControl::GetForTigerLakeTranscoder(
          tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x62420u, transcoder_variable_rate_refresh_control_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x63420.
}

TEST(TranscoderChickenTest, GetForKabyLakeDdi) {
  // The register MMIO addresses come from the Kaby Lake Workarounds description
  // at IHD-OS-KBL-Vol 16-1.17 page 30.
  //
  // The registers used by DDIs A-D are not the same as the registers used by
  // the transcoders A-D. This can be confirmed by cross-checking the
  // workarounds with BSpec IDs 1143 and 1144, on pages 30-31.

  auto transcoder_chicken_a =
      tgl_registers::TranscoderChicken::GetForKabyLakeDdi(DdiId::DDI_B).FromValue(0);
  EXPECT_EQ(0x420c0u, transcoder_chicken_a.reg_addr());

  auto transcoder_chicken_b =
      tgl_registers::TranscoderChicken::GetForKabyLakeDdi(DdiId::DDI_C).FromValue(0);
  EXPECT_EQ(0x420c4u, transcoder_chicken_b.reg_addr());

  auto transcoder_chicken_c =
      tgl_registers::TranscoderChicken::GetForKabyLakeDdi(DdiId::DDI_D).FromValue(0);
  EXPECT_EQ(0x420c8u, transcoder_chicken_c.reg_addr());

  auto transcoder_chicken_edp =
      tgl_registers::TranscoderChicken::GetForKabyLakeDdi(DdiId::DDI_A).FromValue(0);
  EXPECT_EQ(0x420ccu, transcoder_chicken_edp.reg_addr());
}

TEST(TranscoderChickenTest, GetForKabyLakeTranscoder) {
  // The register MMIO addresses come from the Kaby Lake Workarounds description
  // at IHD-OS-KBL-Vol 16-1.17 page 31.

  auto transcoder_chicken_a =
      tgl_registers::TranscoderChicken::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x420c0u, transcoder_chicken_a.reg_addr());

  auto transcoder_chicken_b =
      tgl_registers::TranscoderChicken::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x420c4u, transcoder_chicken_b.reg_addr());

  auto transcoder_chicken_c =
      tgl_registers::TranscoderChicken::GetForKabyLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x420c8u, transcoder_chicken_c.reg_addr());
}

TEST(TranscoderChickenTest, GetForTigerLakeTranscoder) {
  // The register MMIO addresses come from the reference manual.
  //
  // Tiger Lake: IHD-OS-DG1-Vol 12-2.21 page 192

  auto transcoder_chicken_a =
      tgl_registers::TranscoderChicken::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_A)
          .FromValue(0);
  EXPECT_EQ(0x420c0u, transcoder_chicken_a.reg_addr());

  auto transcoder_chicken_b =
      tgl_registers::TranscoderChicken::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_B)
          .FromValue(0);
  EXPECT_EQ(0x420c4u, transcoder_chicken_b.reg_addr());

  auto transcoder_chicken_c =
      tgl_registers::TranscoderChicken::GetForTigerLakeTranscoder(tgl_registers::Trans::TRANS_C)
          .FromValue(0);
  EXPECT_EQ(0x420c8u, transcoder_chicken_c.reg_addr());

  // TODO(fxbug.dev/109278): Add a test for transcoder D, when we support it.
  // The MMIO address is 0x420d8.
}

}  // namespace

}  // namespace i915_tgl
