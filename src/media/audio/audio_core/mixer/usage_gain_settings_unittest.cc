// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/usage_gain_settings.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

constexpr float kArbitraryGainValue = -45.0;
constexpr float kArbitraryGainAdjustment = -2.0;

TEST(UsageGainSettingsTest, BasicRenderUsageGainPersists) {
  UsageGainSettings under_test;

  const auto test_usage = [&under_test](auto render_usage) {
    under_test.SetUsageGain(UsageFrom(render_usage), kArbitraryGainValue);
    EXPECT_FLOAT_EQ(under_test.GetUsageGain(UsageFrom(render_usage)), kArbitraryGainValue);

    under_test.SetUsageGainAdjustment(UsageFrom(render_usage), kArbitraryGainAdjustment);
    EXPECT_FLOAT_EQ(under_test.GetUsageGain(UsageFrom(render_usage)),
                    kArbitraryGainValue + kArbitraryGainAdjustment);
  };

  test_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
  test_usage(fuchsia::media::AudioRenderUsage::MEDIA);
  test_usage(fuchsia::media::AudioRenderUsage::INTERRUPTION);
  test_usage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  test_usage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
}

TEST(UsageGainSettingsTest, BasicCaptureUsageGainPersists) {
  UsageGainSettings under_test;

  const auto test_usage = [&under_test](auto capture_usage) {
    under_test.SetUsageGain(UsageFrom(capture_usage), kArbitraryGainValue);
    EXPECT_FLOAT_EQ(under_test.GetUsageGain(UsageFrom(capture_usage)), kArbitraryGainValue);

    under_test.SetUsageGainAdjustment(UsageFrom(capture_usage), kArbitraryGainAdjustment);
    EXPECT_FLOAT_EQ(under_test.GetUsageGain(UsageFrom(capture_usage)),
                    kArbitraryGainValue + kArbitraryGainAdjustment);
  };

  test_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  test_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
}

}  // namespace
}  // namespace media::audio
