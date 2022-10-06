// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fidl/fuchsia.audio/cpp/wire_types.h"
#include "src/media/audio/lib/processing/sampler.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::media::TimelineRate;

constexpr int64_t kFrameCount = 3;

class SamplerDeathTest : public testing::TestWithParam<Sampler::Type> {
 protected:
  SamplerDeathTest() : source_samples_(kFrameCount), dest_samples_(kFrameCount) {}

  void SetUp() override {
    sampler_ = Sampler::Create(Format::CreateOrDie({SampleType::kFloat32, 1, 48000}),
                               Format::CreateOrDie({SampleType::kFloat32, 1, 48000}), GetParam());
    ASSERT_NE(sampler_, nullptr) << "Sampler could not be created with default parameters";
  }

  void Process(Fixed source_frame_offset, int64_t dest_frame_offset,
               int64_t frame_count = kFrameCount) {
    sampler_->Process({source_samples_.data(), &source_frame_offset, frame_count},
                      {dest_samples_.data(), &dest_frame_offset, frame_count}, {},
                      /*accumulate=*/false);
  }

  Sampler& sampler() { return *sampler_; }

 private:
  std::shared_ptr<Sampler> sampler_;
  std::vector<float> source_samples_;
  std::vector<float> dest_samples_;
};

TEST_P(SamplerDeathTest, BaselineShouldSucceed) { Process(Fixed(0), 0); }

TEST_P(SamplerDeathTest, DestPositionTooLow) { EXPECT_DEATH(Process(Fixed(0), -1), ""); }

TEST_P(SamplerDeathTest, DestPositionTooHigh) {
  Process(Fixed(0), kFrameCount - 1);

  EXPECT_DEATH(Process(Fixed(0), kFrameCount), "");
}

TEST_P(SamplerDeathTest, SourceFramesTooLow) {
  Process(Fixed(0), 0, 1);

  EXPECT_DEATH(Process(Fixed(0), 0, 0), "");
}

TEST_P(SamplerDeathTest, SourcePositionTooLow) {
  Process(Fixed(0) - sampler().pos_filter_length() + Fixed::FromRaw(1), 0);

  EXPECT_DEATH(Process(Fixed(0) - sampler().pos_filter_length(), 0), "");
}

TEST_P(SamplerDeathTest, SourcePositionTooHigh) {
  Process(Fixed(kFrameCount), 0);

  EXPECT_DEATH(Process(Fixed(kFrameCount) + Fixed::FromRaw(1), 0), "");
}

TEST_P(SamplerDeathTest, StepSizeTooLow) {
  sampler().state().ResetSourceStride(TimelineRate(1, 1));
  Process(Fixed(0), 0);

  sampler().state().ResetSourceStride(TimelineRate(0, 1));
  EXPECT_DEATH(Process(Fixed(0), 0), "");
}

TEST_P(SamplerDeathTest, SourcePosModuloTooHigh) {
  sampler().state().ResetSourceStride(TimelineRate(Fixed(64).raw_value(), 243));
  sampler().state().set_source_pos_modulo(242);
  Process(Fixed(0), 0);

  sampler().state().set_source_pos_modulo(243);
  EXPECT_DEATH(Process(Fixed(0), 0), "");
}

template <typename TestClass>
std::string PrintSamplerTypeParam(
    const ::testing::TestParamInfo<typename TestClass::ParamType>& info) {
  switch (info.param) {
    case Sampler::Type::kDefault:
      return "Default";
    case Sampler::Type::kSincSampler:
      return "Sinc";
    default:
      return "Unknown";
  }
}

#define INSTANTIATE_SYNC_TEST_SUITE(_test_class_name)                                             \
  INSTANTIATE_TEST_SUITE_P(DeathTesting, _test_class_name,                                        \
                           testing::Values(Sampler::Type::kDefault, Sampler::Type::kSincSampler), \
                           PrintSamplerTypeParam<_test_class_name>)

INSTANTIATE_SYNC_TEST_SUITE(SamplerDeathTest);

}  // namespace
}  // namespace media_audio
