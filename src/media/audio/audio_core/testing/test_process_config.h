// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_TEST_PROCESS_CONFIG_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_TEST_PROCESS_CONFIG_H_

#include "src/media/audio/audio_core/process_config.h"

namespace media::audio::testing {

// Helper for testing code that relies on a ProcessConfig being set. The default constructor
// provides a reasonable/sane default configuration.
class TestProcessConfig {
 public:
  TestProcessConfig(ProcessConfig config)
      : config_handle_{ProcessConfig::set_instance(std::move(config))} {}

  TestProcessConfig()
      : TestProcessConfig(
            ProcessConfig::Builder()
                .AddDeviceRoutingProfile(
                    {std::nullopt,
                     RoutingConfig::DeviceProfile(
                         /* eligible_for_loopback */ true,
                         {fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND),
                          fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA),
                          fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::INTERRUPTION),
                          fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT),
                          fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::COMMUNICATION)})})
                .SetDefaultVolumeCurve(
                    VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
                .Build()) {}

 private:
  ProcessConfig::Handle config_handle_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_TEST_PROCESS_CONFIG_H_
