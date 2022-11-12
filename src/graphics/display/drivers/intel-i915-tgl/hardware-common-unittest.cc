// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace i915_tgl {

TEST(HardwareCommonTest, Skylake) {
  // Skylake has 5 DDIs (A-E), 4 DPLLs, 3 Pipes and 4 Transcoders
  // (including an eDP transcoder).

  auto ddis = DdiIds<tgl_registers::Platform::kSkylake>();
  EXPECT_EQ(ddis.size(), 5u);
  EXPECT_THAT(ddis, testing::Contains(DdiId::DDI_A).Times(1));
  EXPECT_THAT(ddis, testing::Contains(DdiId::DDI_E).Times(1));
  EXPECT_THAT(ddis, testing::Not(testing::Contains(DdiId::DDI_TC_6)));

  auto dplls = tgl_registers::Dplls<tgl_registers::Platform::kSkylake>();
  EXPECT_EQ(dplls.size(), 4u);
  EXPECT_THAT(dplls, testing::Contains(tgl_registers::DPLL_0).Times(1));
  EXPECT_THAT(dplls, testing::Contains(tgl_registers::DPLL_3).Times(1));

  auto pipes = PipeIds<tgl_registers::Platform::kSkylake>();
  EXPECT_EQ(pipes.size(), 3u);
  EXPECT_THAT(pipes, testing::UnorderedElementsAre(PipeId::PIPE_A, PipeId::PIPE_B, PipeId::PIPE_C));

  auto transcoders = TranscoderIds<tgl_registers::Platform::kSkylake>();
  EXPECT_EQ(transcoders.size(), 4u);
  EXPECT_THAT(transcoders, testing::UnorderedElementsAre(
                               TranscoderId::TRANSCODER_EDP, TranscoderId::TRANSCODER_A,
                               TranscoderId::TRANSCODER_B, TranscoderId::TRANSCODER_C));
}

TEST(HardwareCommonTest, KabyLake) {
  // Kaby Lake has 5 DDIs (A-E), 4 DPLLs, 3 Pipes and 4 Transcoders
  // (including an eDP transcoder).

  auto ddis = DdiIds<tgl_registers::Platform::kKabyLake>();
  EXPECT_EQ(ddis.size(), 5u);
  EXPECT_THAT(ddis, testing::Contains(DdiId::DDI_A).Times(1));
  EXPECT_THAT(ddis, testing::Contains(DdiId::DDI_E).Times(1));
  EXPECT_THAT(ddis, testing::Not(testing::Contains(DdiId::DDI_TC_6)));

  auto dplls = tgl_registers::Dplls<tgl_registers::Platform::kKabyLake>();
  EXPECT_EQ(dplls.size(), 4u);
  EXPECT_THAT(dplls, testing::Contains(tgl_registers::DPLL_0).Times(1));
  EXPECT_THAT(dplls, testing::Contains(tgl_registers::DPLL_3).Times(1));

  auto pipes = PipeIds<tgl_registers::Platform::kKabyLake>();
  EXPECT_EQ(pipes.size(), 3u);
  EXPECT_THAT(pipes, testing::UnorderedElementsAre(PipeId::PIPE_A, PipeId::PIPE_B, PipeId::PIPE_C));

  auto transcoders = TranscoderIds<tgl_registers::Platform::kKabyLake>();
  EXPECT_EQ(transcoders.size(), 4u);
  EXPECT_THAT(transcoders, testing::UnorderedElementsAre(
                               TranscoderId::TRANSCODER_EDP, TranscoderId::TRANSCODER_A,
                               TranscoderId::TRANSCODER_B, TranscoderId::TRANSCODER_C));
}

TEST(HardwareCommonTest, TigerLake) {
  // Tiger Lake has 9 DDIs (A-C, TC1-TC6), currently it supports 3 Pipes and 3 Transcoders.
  // There is no eDP transcoder in Tiger Lake.

  auto ddis = DdiIds<tgl_registers::Platform::kTigerLake>();
  EXPECT_EQ(ddis.size(), 9u);
  EXPECT_THAT(ddis, testing::Contains(DdiId::DDI_A).Times(1));
  EXPECT_THAT(ddis, testing::Contains(DdiId::DDI_C).Times(1));
  EXPECT_THAT(ddis, testing::Contains(DdiId::DDI_TC_6).Times(1));

  // TODO(fxbug.dev/109278): Update the test once Pipe D is supported.
  auto pipes = PipeIds<tgl_registers::Platform::kTigerLake>();
  EXPECT_EQ(pipes.size(), 3u);
  EXPECT_THAT(pipes, testing::UnorderedElementsAre(PipeId::PIPE_A, PipeId::PIPE_B, PipeId::PIPE_C));

  // TODO(fxbug.dev/109278): Update the test once Transcoder D is supported.
  auto transcoders = TranscoderIds<tgl_registers::Platform::kTigerLake>();
  EXPECT_EQ(transcoders.size(), 3u);
  // There is no eDP Transcoder.
  EXPECT_THAT(transcoders, testing::Not(testing::Contains(TranscoderId::TRANSCODER_EDP)));
  EXPECT_THAT(transcoders, testing::Contains(TranscoderId::TRANSCODER_A));
}

}  // namespace i915_tgl
