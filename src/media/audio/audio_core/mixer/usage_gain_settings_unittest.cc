// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/usage_gain_settings.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

constexpr float kArbitraryGainValue = -45.0;
constexpr float kArbitraryGainAdjustment = -2.0;

fuchsia::media::Usage UsageFrom(fuchsia::media::AudioRenderUsage render_usage) {
  fuchsia::media::Usage usage;
  usage.set_render_usage(render_usage);
  return usage;
}

fuchsia::media::Usage UsageFrom(fuchsia::media::AudioCaptureUsage capture_usage) {
  fuchsia::media::Usage usage;
  usage.set_capture_usage(capture_usage);
  return usage;
}

TEST(UsageGainSettingsTest, BasicRenderUsageGainPersists) {
  UsageGainSettings under_test;

  const auto test_usage = [&under_test](auto render_usage) {
    under_test.SetRenderUsageGain(render_usage, kArbitraryGainValue);
    EXPECT_EQ(under_test.GetUsageGain(UsageFrom(render_usage)), kArbitraryGainValue);

    under_test.SetRenderUsageGainAdjustment(render_usage, kArbitraryGainAdjustment);
    EXPECT_EQ(under_test.GetUsageGain(UsageFrom(render_usage)),
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
    under_test.SetCaptureUsageGain(capture_usage, kArbitraryGainValue);
    EXPECT_EQ(under_test.GetUsageGain(UsageFrom(capture_usage)), kArbitraryGainValue);

    under_test.SetCaptureUsageGainAdjustment(capture_usage, kArbitraryGainAdjustment);
    EXPECT_EQ(under_test.GetUsageGain(UsageFrom(capture_usage)),
              kArbitraryGainValue + kArbitraryGainAdjustment);
  };

  test_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  test_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
}

}  // namespace
}  // namespace media::audio
