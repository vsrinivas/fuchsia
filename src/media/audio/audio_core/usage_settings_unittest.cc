// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/usage_settings.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/gain.h"

namespace media::audio {
namespace {

constexpr float kArbitraryGainValue = -45.0;
constexpr float kArbitraryGainAdjustment = -2.0;

constexpr float kArbitraryVolumeValue = 0.14;

TEST(UsageGainSettingsTest, RenderUsageGainPersists) {
  UsageGainSettings under_test;

  const auto test_usage = [&under_test](auto render_usage) {
    under_test.SetUsageGain(UsageFrom(render_usage), kArbitraryGainValue);
    EXPECT_FLOAT_EQ(under_test.GetUsageGain(UsageFrom(render_usage)), kArbitraryGainValue);

    under_test.SetUsageGainAdjustment(UsageFrom(render_usage), kArbitraryGainAdjustment);
    EXPECT_FLOAT_EQ(under_test.GetUsageGain(UsageFrom(render_usage)),
                    kArbitraryGainValue + kArbitraryGainAdjustment);
  };

  test_usage(fuchsia::media::AudioRenderUsage::MEDIA);
  test_usage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
}

TEST(UsageGainSettingsTest, CaptureUsageGainPersists) {
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

TEST(UsageGainSettingsTest, UsageGainCannotExceedUnity) {
  const auto usage = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  UsageGainSettings under_test;
  under_test.SetUsageGain(fidl::Clone(usage), 10.0);

  EXPECT_FLOAT_EQ(under_test.GetUsageGain(fidl::Clone(usage)), Gain::kUnityGainDb);
}

TEST(UsageVolumeSettingsTest, RenderUsageVolumePersists) {
  UsageVolumeSettings under_test;

  const auto test_usage = [&under_test](auto render_usage) {
    under_test.SetUsageVolume(UsageFrom(render_usage), kArbitraryVolumeValue);
    EXPECT_FLOAT_EQ(under_test.GetUsageVolume(UsageFrom(render_usage)), kArbitraryVolumeValue);
  };

  test_usage(fuchsia::media::AudioRenderUsage::BACKGROUND);
  test_usage(fuchsia::media::AudioRenderUsage::MEDIA);
  test_usage(fuchsia::media::AudioRenderUsage::INTERRUPTION);
  test_usage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  test_usage(fuchsia::media::AudioRenderUsage::COMMUNICATION);
}

TEST(UsageVolumeSettingsTest, CaptureUsageVolumePersists) {
  UsageVolumeSettings under_test;

  const auto test_usage = [&under_test](auto capture_usage) {
    under_test.SetUsageVolume(UsageFrom(capture_usage), kArbitraryVolumeValue);
    EXPECT_FLOAT_EQ(under_test.GetUsageVolume(UsageFrom(capture_usage)), kArbitraryVolumeValue);
  };

  test_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  test_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
}

TEST(UsageVolumeSettingsTest, DefaultVolumeIsMax) {
  UsageVolumeSettings under_test;

  const auto test_usage = [&under_test](auto capture_usage) {
    EXPECT_FLOAT_EQ(under_test.GetUsageVolume(UsageFrom(capture_usage)),
                    fuchsia::media::audio::MAX_VOLUME);
  };

  test_usage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  test_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
}

}  // namespace
}  // namespace media::audio
