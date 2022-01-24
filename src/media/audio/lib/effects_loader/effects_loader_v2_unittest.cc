// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_loader_v2.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/effects/test_effects/test_effects_v2.h"

namespace media::audio {
namespace {

zx_status_t AddOne(uint64_t num_frames, float* input, float* output,
                   float total_applied_gain_for_input,
                   std::vector<fuchsia_audio_effects::wire::ProcessMetrics>& metrics) {
  for (uint64_t k = 0; k < num_frames; k++) {
    output[k] = input[k] + 1;
  }
  return ZX_OK;
}

float* MapBufferOrDie(fzl::VmoMapper& mapper, const fuchsia_mem::wire::Range& range) {
  if (auto status = mapper.Map(range.vmo, range.offset, range.size); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to map buffer with offset,size = " << range.offset << ","
                            << range.size;
  }
  return reinterpret_cast<float*>(reinterpret_cast<char*>(mapper.start()) + range.offset);
}

class EffectsLoaderV2Test : public ::testing::Test {
 protected:
  TestEffectsV2 server_;
};

TEST_F(EffectsLoaderV2Test, CreateEffect) {
  auto loader_result = EffectsLoaderV2::CreateFromChannel(server_.NewClient());
  ASSERT_TRUE(loader_result.is_ok());

  // Add a simple effect.
  constexpr size_t kNumFrames = 10;
  server_.AddEffect({
      .name = "AddOne",
      .process = &AddOne,
      .process_in_place = true,
      .max_frames_per_call = kNumFrames,
      .frames_per_second = 48000,
      .input_channels = 1,
      .output_channels = 1,
  });

  // Verify the effect is available.
  auto loader = loader_result.take_value();
  auto config_result = loader->GetProcessorConfiguration("AddOne");
  ASSERT_TRUE(config_result.ok());

  // Check a few fields to make sure the config matches expectations.
  auto& config = config_result->result.response().processor_configuration;
  ASSERT_TRUE(config.has_inputs());
  ASSERT_TRUE(config.has_outputs());
  EXPECT_EQ(config.inputs().count(), 1u);
  EXPECT_EQ(config.outputs().count(), 1u);
  EXPECT_EQ(config.inputs()[0].format().channel_count, 1u);
  EXPECT_EQ(config.outputs()[0].format().channel_count, 1u);
  EXPECT_EQ(config.outputs()[0].latency_frames(), 0u);
  EXPECT_EQ(config.max_frames_per_call(), kNumFrames);

  // Load the VMOs into our address space.
  fzl::VmoMapper input_mapper;
  fzl::VmoMapper output_mapper;
  float* input = MapBufferOrDie(input_mapper, config.inputs()[0].buffer());
  float* output = MapBufferOrDie(output_mapper, config.outputs()[0].buffer());

  memset(input, 0, kNumFrames * sizeof(float));
  memset(output, 0, kNumFrames * sizeof(float));

  // Verify the effect works.
  fidl::Arena arena;
  auto processor = fidl::BindSyncClient(std::move(config.processor()));
  auto result = processor->Process(kNumFrames, fuchsia_audio_effects::wire::ProcessOptions(arena));
  EXPECT_EQ(result.status(), ZX_OK);
  EXPECT_FALSE(result->result.is_err()) << "unexpected failure: " << result->result.err();

  EXPECT_THAT((reinterpret_cast<std::array<float, kNumFrames>&>(output[0])),
              ::testing::Each(::testing::FloatEq(1.0f)));
}

TEST_F(EffectsLoaderV2Test, EffectDoesNotExist) {
  auto loader_result = EffectsLoaderV2::CreateFromChannel(server_.NewClient());
  ASSERT_TRUE(loader_result.is_ok());

  auto loader = loader_result.take_value();
  auto result = loader->GetProcessorConfiguration("DoesNotExist");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result->result.is_err());
}

}  // namespace
}  // namespace media::audio
