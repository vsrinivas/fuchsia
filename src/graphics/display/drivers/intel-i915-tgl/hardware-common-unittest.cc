// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace tgl_registers {

TEST(HardwareCommonTest, Skylake) {
  // Skylake has 5 DDIs (A-E), 4 DPLLs, 3 Pipes and 4 Transcoders
  // (including an eDP transcoder).

  auto ddis = i915_tgl::DdiIds<Platform::kSkylake>();
  EXPECT_EQ(ddis.size(), 5u);
  EXPECT_THAT(ddis, testing::Contains(i915_tgl::DdiId::DDI_A).Times(1));
  EXPECT_THAT(ddis, testing::Contains(i915_tgl::DdiId::DDI_E).Times(1));
  EXPECT_THAT(ddis, testing::Not(testing::Contains(i915_tgl::DdiId::DDI_TC_6)));

  auto dplls = Dplls<Platform::kSkylake>();
  EXPECT_EQ(dplls.size(), 4u);
  EXPECT_THAT(dplls, testing::Contains(DPLL_0).Times(1));
  EXPECT_THAT(dplls, testing::Contains(DPLL_3).Times(1));

  auto pipes = Pipes<Platform::kSkylake>();
  EXPECT_EQ(pipes.size(), 3u);
  EXPECT_THAT(pipes, testing::UnorderedElementsAre(PIPE_A, PIPE_B, PIPE_C));

  auto transcoders = Transcoders<Platform::kSkylake>();
  EXPECT_EQ(transcoders.size(), 4u);
  EXPECT_THAT(transcoders, testing::UnorderedElementsAre(TRANS_EDP, TRANS_A, TRANS_B, TRANS_C));
}

TEST(HardwareCommonTest, KabyLake) {
  // Kaby Lake has 5 DDIs (A-E), 4 DPLLs, 3 Pipes and 4 Transcoders
  // (including an eDP transcoder).

  auto ddis = i915_tgl::DdiIds<Platform::kKabyLake>();
  EXPECT_EQ(ddis.size(), 5u);
  EXPECT_THAT(ddis, testing::Contains(i915_tgl::DdiId::DDI_A).Times(1));
  EXPECT_THAT(ddis, testing::Contains(i915_tgl::DdiId::DDI_E).Times(1));
  EXPECT_THAT(ddis, testing::Not(testing::Contains(i915_tgl::DdiId::DDI_TC_6)));

  auto dplls = Dplls<Platform::kKabyLake>();
  EXPECT_EQ(dplls.size(), 4u);
  EXPECT_THAT(dplls, testing::Contains(DPLL_0).Times(1));
  EXPECT_THAT(dplls, testing::Contains(DPLL_3).Times(1));

  auto pipes = Pipes<Platform::kKabyLake>();
  EXPECT_EQ(pipes.size(), 3u);
  EXPECT_THAT(pipes, testing::UnorderedElementsAre(PIPE_A, PIPE_B, PIPE_C));

  auto transcoders = Transcoders<Platform::kKabyLake>();
  EXPECT_EQ(transcoders.size(), 4u);
  EXPECT_THAT(transcoders, testing::UnorderedElementsAre(TRANS_EDP, TRANS_A, TRANS_B, TRANS_C));
}

TEST(HardwareCommonTest, TigerLake) {
  // Tiger Lake has 9 DDIs (A-C, TC1-TC6), currently it supports 3 Pipes and 3 Transcoders.
  // There is no eDP transcoder in Tiger Lake.

  auto ddis = i915_tgl::DdiIds<Platform::kTigerLake>();
  EXPECT_EQ(ddis.size(), 9u);
  EXPECT_THAT(ddis, testing::Contains(i915_tgl::DdiId::DDI_A).Times(1));
  EXPECT_THAT(ddis, testing::Contains(i915_tgl::DdiId::DDI_C).Times(1));
  EXPECT_THAT(ddis, testing::Contains(i915_tgl::DdiId::DDI_TC_6).Times(1));

  // TODO(fxbug.dev/109278): Update the test once Pipe D is supported.
  auto pipes = Pipes<Platform::kTigerLake>();
  EXPECT_EQ(pipes.size(), 3u);
  EXPECT_THAT(pipes, testing::UnorderedElementsAre(PIPE_A, PIPE_B, PIPE_C));

  // TODO(fxbug.dev/109278): Update the test once Transcoder D is supported.
  auto transcoders = Transcoders<Platform::kTigerLake>();
  EXPECT_EQ(transcoders.size(), 3u);
  // There is no eDP Transcoder.
  EXPECT_THAT(transcoders, testing::Not(testing::Contains(TRANS_EDP)));
  EXPECT_THAT(transcoders, testing::Contains(TRANS_A));
}

}  // namespace tgl_registers
