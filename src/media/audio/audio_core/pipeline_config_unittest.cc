// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/pipeline_config.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

TEST(PipelineConfigTest, CalculateChannels) {
  auto config = PipelineConfig::Default();

  // No effects, the pipeline channelization is the same as the output of the root mix stage.
  EXPECT_EQ(config.root().output_channels, config.channels());

  // With rechannelization effects, the last effect defines the channelization.
  config.mutable_root().effects_v1.push_back(
      {"lib.so", "effect", "e1", "", {config.root().output_channels + 1}});
  config.mutable_root().effects_v1.push_back(
      {"lib.so", "effect", "e2", "", {config.root().output_channels + 2}});
  EXPECT_EQ(config.root().output_channels + 2, config.channels());
}

}  // namespace
}  // namespace media::audio
