// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/thermal_watcher.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

#include <cstdint>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/audio_core/shared/device_id.h"
#include "src/media/audio/audio_core/shared/thermal_config.h"
#include "src/media/audio/audio_core/v1/context.h"
#include "src/media/audio/audio_core/v1/testing/fake_plug_detector.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"

namespace media::audio {

namespace {

const auto kDeviceIdString = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
const auto kDeviceIdUnique = DeviceUniqueIdFromString(kDeviceIdString).take_value();
const auto kDefaultVolumeCurve = VolumeCurve::DefaultForMinGain(-160.0f);

}  // namespace

class ThermalWatcherTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override { TestLoopFixture::SetUp(); }

  std::unique_ptr<Context> CreateContext(ProcessConfig process_config) {
    auto threading_model = std::make_unique<testing::TestThreadingModel>(&test_loop());
    sys::testing::ComponentContextProvider component_context_provider_;
    auto plug_detector = std::make_unique<testing::FakePlugDetector>();
    return Context::Create(std::move(threading_model), component_context_provider_.TakeContext(),
                           std::move(plug_detector), std::move(process_config),
                           std::make_shared<AudioCoreClockFactory>());
  }

 private:
};

// These tests really only test thermal config parsing (already tested via
// process_config_loader_unittest.cc). That said, they are as far as is possible at the unittest
// level, since the next step is to use HermeticAudioRealm and "real FIDL", injecting mock
// fuchsia::thermal::ClientStateConnector and fuchsia::thermal::ClientStateWatcher servers.
//
// For a config that specifies no thermal states, we don't even create a thermal watcher.
TEST_F(ThermalWatcherTest, NoWatcherIfNoConfig) {
  auto empty_context =
      CreateContext(ProcessConfig::Builder().SetDefaultVolumeCurve(kDefaultVolumeCurve).Build());
  auto watcher = ThermalWatcher::CreateAndWatch(*empty_context);

  EXPECT_EQ(empty_context->process_config().thermal_config().states().size(), 0u);
  EXPECT_EQ(watcher, nullptr);
}

TEST_F(ThermalWatcherTest, MultipleStatesAndEffects) {
  std::string instance0_name = "my_eq";
  std::string instance1_name = "my_reverb";
  std::string initial_config0 = "initial_config0";
  std::string initial_config1 = "initial_config1";
  std::string second_config0 = "second_config0";
  std::string second_config1 = "second_config1";

  ThermalConfig::EffectConfig effect0_config0{instance0_name.c_str(), initial_config0.c_str()};
  ThermalConfig::EffectConfig effect0_config1{instance0_name.c_str(), second_config0.c_str()};
  ThermalConfig::EffectConfig effect1_config0{instance1_name.c_str(), initial_config1.c_str()};
  ThermalConfig::EffectConfig effect1_config1{instance1_name.c_str(), second_config1.c_str()};

  auto thermal_state0 = ThermalConfig::State(0, {effect0_config0, effect1_config0});
  auto thermal_state1 = ThermalConfig::State(1, {effect0_config1, effect1_config1});

  const auto kTwoThermalStatesConfig =
      ProcessConfigBuilder()
          .SetDefaultVolumeCurve(kDefaultVolumeCurve)
          .AddDeviceProfile(
              {std::vector<audio_stream_unique_id_t>{kDeviceIdUnique},
               DeviceConfig::OutputDeviceProfile(
                   /*eligible_for_loopback=*/true, /*supported_usages=*/{}, kDefaultVolumeCurve,
                   /*independent_volume_control=*/false,
                   PipelineConfig(PipelineConfig::MixGroup{
                       .name = "linearize",
                       .input_streams = {RenderUsage::BACKGROUND, RenderUsage::MEDIA},
                       .effects_v1 =
                           {
                               PipelineConfig::EffectV1{
                                   .lib_name = "my_effects.so",
                                   .effect_name = "equalizer",
                                   .instance_name = instance0_name,
                                   .effect_config = initial_config0,
                                   .output_channels = 1,
                               },
                               PipelineConfig::EffectV1{
                                   .lib_name = "my_effects.so",
                                   .effect_name = "reverb",
                                   .instance_name = instance1_name,
                                   .effect_config = initial_config1,
                                   .output_channels = 1,
                               },
                           },
                       .inputs = {},
                       .loopback = true,
                       .output_rate = 48000,
                       .output_channels = 1}),
                   /*driver_gain_db=*/0.0, /*software_gain_db=*/0.0)})
          .AddThermalConfigState(std::move(thermal_state0))
          .AddThermalConfigState(std::move(thermal_state1))
          .Build();

  auto context = CreateContext(kTwoThermalStatesConfig);

  const auto& thermal_config = context->process_config().thermal_config();
  EXPECT_EQ(thermal_config.states().size(), 2u);

  EXPECT_EQ(thermal_config.states()[0].thermal_state_number(), 0u);
  EXPECT_EQ(thermal_config.states()[0].effect_configs().size(), 2u);
  EXPECT_EQ(thermal_config.states()[0].effect_configs()[0].name(), instance0_name);
  EXPECT_EQ(thermal_config.states()[0].effect_configs()[0].config_string(), initial_config0);
  EXPECT_EQ(thermal_config.states()[0].effect_configs()[1].name(), instance1_name);
  EXPECT_EQ(thermal_config.states()[0].effect_configs()[1].config_string(), initial_config1);

  EXPECT_EQ(thermal_config.states()[1].thermal_state_number(), 1u);
  EXPECT_EQ(thermal_config.states()[1].effect_configs().size(), 2u);
  EXPECT_EQ(thermal_config.states()[1].effect_configs()[0].name(), instance0_name);
  EXPECT_EQ(thermal_config.states()[1].effect_configs()[0].config_string(), second_config0);
  EXPECT_EQ(thermal_config.states()[1].effect_configs()[1].name(), instance1_name);
  EXPECT_EQ(thermal_config.states()[1].effect_configs()[1].config_string(), second_config1);

  auto watcher = ThermalWatcher::CreateAndWatch(*context);
  EXPECT_NE(watcher, nullptr);
}

}  // namespace media::audio
