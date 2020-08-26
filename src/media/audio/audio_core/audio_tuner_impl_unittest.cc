// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_tuner_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects.h"

using ::testing::AllOf;
using ::testing::Field;

namespace media::audio {
namespace {

const auto kDeviceIdString = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
const auto kDeviceIdUnique = AudioDevice::UniqueIdFromString(kDeviceIdString).take_value();

const auto kVolumeCurve = VolumeCurve::DefaultForMinGain(-160.0f);
const auto kDefaultProcessConfig =
    ProcessConfig::Builder().SetDefaultVolumeCurve(kVolumeCurve).Build();
const auto kDefaultPipelineConfig = PipelineConfig::Default();

void ExpectEq(const VolumeCurve& expected,
              const std::vector<fuchsia::media::tuning::Volume>& result) {
  std::vector<VolumeCurve::VolumeMapping> expected_mappings = expected.mappings();
  EXPECT_EQ(expected_mappings.size(), result.size());
  for (size_t i = 0; i < expected_mappings.size(); ++i) {
    EXPECT_EQ(expected_mappings[i].volume, result[i].level);
    EXPECT_EQ(expected_mappings[i].gain_dbfs, result[i].decibel);
  }
}

void ExpectEq(const PipelineConfig::Effect& expected,
              const fuchsia::media::tuning::AudioEffectConfig& result) {
  EXPECT_EQ(expected.lib_name, result.type().module_name());
  EXPECT_EQ(expected.effect_name, result.type().effect_name());
  EXPECT_EQ(expected.instance_name, result.instance_name());
  EXPECT_EQ(expected.effect_config, result.configuration());
  EXPECT_EQ(expected.output_channels, result.output_channels());
}

void ExpectEq(const PipelineConfig::MixGroup& expected,
              const fuchsia::media::tuning::AudioMixGroup& result) {
  EXPECT_EQ(expected.name, result.name);
  EXPECT_EQ(expected.loopback, result.loopback);
  EXPECT_EQ(expected.input_streams.size(), result.streams.size());
  for (size_t i = 0; i < expected.input_streams.size(); ++i) {
    auto expected_usage = expected.input_streams[i];
    auto result_usage = result.streams[i];
    EXPECT_EQ(StreamTypeFromRenderUsage(expected_usage), result_usage);
  }
  EXPECT_EQ(expected.effects.size(), result.effects.size());
  for (size_t i = 0; i < expected.effects.size(); ++i) {
    ExpectEq(expected.effects[i], result.effects[i]);
  }
  EXPECT_EQ(expected.inputs.size(), result.inputs.size());
  for (size_t i = 0; i < expected.inputs.size(); ++i) {
    ExpectEq(expected.inputs[i], *result.inputs[i].get());
  }
  EXPECT_EQ(expected.output_rate, result.output_rate);
  EXPECT_EQ(expected.output_channels, result.output_channels);
}

class TestDevice : public AudioOutput {
 public:
  TestDevice(std::unique_ptr<Context>& context)
      : AudioOutput("", &context->threading_model(), &context->device_manager(),
                    &context->link_matrix()) {
    zx::channel c1, c2;
    ZX_ASSERT(ZX_OK == zx::channel::create(0, &c1, &c2));
    fake_driver_ = std::make_unique<testing::FakeAudioDriverV1>(
        std::move(c1), context->threading_model().FidlDomain().dispatcher());
    fake_driver_->set_stream_unique_id(kDeviceIdUnique);
    ZX_ASSERT(ZX_OK == driver()->Init(std::move(c2)));
    fake_driver_->Start();
    driver()->GetDriverInfo();
  };

  void UpdatePlugState(bool plugged) { AudioDevice::UpdatePlugState(plugged, plug_time()); }
  void CompleteUpdates() {
    for (auto& bridge : pipeline_update_bridges_) {
      bridge.completer.complete_ok();
    }
    pipeline_update_bridges_.clear();

    for (auto& bridge : effect_update_bridges_) {
      bridge.completer.complete_ok();
    }
    effect_update_bridges_.clear();
  }

  // AudioDevice
  fit::promise<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config) override {
    effect_update_bridges_.emplace_back();
    return effect_update_bridges_.back().consumer.promise();
  }
  fit::promise<void, zx_status_t> UpdateDeviceProfile(
      const DeviceConfig::OutputDeviceProfile::Parameters& params,
      const VolumeCurve& volume_curve) override {
    pipeline_update_bridges_.emplace_back();
    return pipeline_update_bridges_.back().consumer.promise();
  }
  fuchsia::media::AudioDeviceInfo GetDeviceInfo() const override {
    return {
        .name = driver()->manufacturer_name() + ' ' + driver()->product_name(),
        .unique_id = UniqueIdToString(driver()->persistent_unique_id()),
        .token_id = token(),
        .is_input = is_input(),
        .gain_info =
            {
                .gain_db = 0.0,
                .flags = fuchsia::media::AudioGainInfoFlags{},
            },
        .is_default = true,
    };
  }
  fit::promise<void, zx_status_t> Startup() override {
    return fit::make_result_promise<void, zx_status_t>(fit::ok());
  }
  fit::promise<void> Shutdown() override { return fit::make_ok_promise(); }
  void OnWakeup() override {}
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       fuchsia::media::AudioGainValidFlags set_flags) override {}

  // AudioOutput
  std::optional<AudioOutput::FrameSpan> StartMixJob(zx::time device_ref_time) override {
    return std::nullopt;
  }
  void FinishMixJob(const AudioOutput::FrameSpan& span, float* buffer) override {}
  zx::duration MixDeadline() const override { return zx::msec(10); }

 private:
  std::vector<fit::bridge<void, zx_status_t>> pipeline_update_bridges_;
  std::vector<fit::bridge<void, fuchsia::media::audio::UpdateEffectError>> effect_update_bridges_;
  std::unique_ptr<testing::FakeAudioDriverV1> fake_driver_;
};

class AudioTunerTest : public gtest::TestLoopFixture {
 protected:
  std::unique_ptr<Context> CreateContext(ProcessConfig process_config) {
    handle_ = ProcessConfig::set_instance(process_config);
    auto threading_model = std::make_unique<testing::TestThreadingModel>(&test_loop());
    sys::testing::ComponentContextProvider component_context_provider_;
    auto plug_detector = std::make_unique<testing::FakePlugDetector>();
    return Context::Create(std::move(threading_model), component_context_provider_.TakeContext(),
                           std::move(plug_detector), process_config);
  }

  std::unique_ptr<Context> CreateContext() {
    auto process_config =
        ProcessConfigBuilder()
            .SetDefaultVolumeCurve(kVolumeCurve)
            .AddDeviceProfile({std::vector<audio_stream_unique_id_t>{kDeviceIdUnique},
                               DeviceConfig::OutputDeviceProfile()})
            .Build();
    return CreateContext(process_config);
  }

  testing::TestEffectsModule test_effects_ = testing::TestEffectsModule::Open();
  ProcessConfig::Handle handle_;
};

TEST_F(AudioTunerTest, PlugDuringPipelineConfigUpdate) {
  auto context = CreateContext();
  AudioTunerImpl under_test(*context);

  // Prepare device to be updated.
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  context->device_manager().ActivateDevice(device);

  // Ensure device is unplugged, then begin update.
  EXPECT_FALSE(device->plugged());
  bool completed_update = false;
  auto new_profile = ToAudioDeviceTuningProfile(kDefaultPipelineConfig, kVolumeCurve);
  under_test.SetAudioDeviceProfile(kDeviceIdString, std::move(new_profile),
                                   [&completed_update](zx_status_t result) {
                                     completed_update = true;
                                     EXPECT_EQ(ZX_OK, result);
                                   });

  // Plug in device during update, and verify device is not yet added to RouteGraph.
  context->device_manager().OnPlugStateChanged(device, true, device->plug_time());
  EXPECT_TRUE(device->plugged());
  EXPECT_FALSE(context->route_graph().ContainsDevice(device.get()));

  // Complete update, and verify device is then added to RouteGraph upon update.
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_update);
  EXPECT_TRUE(context->route_graph().ContainsDevice(device.get()));
}

TEST_F(AudioTunerTest, UnplugDuringPipelineConfigUpdate) {
  auto context = CreateContext();
  AudioTunerImpl under_test(*context);

  // Prepare device to be updated.
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  device->UpdatePlugState(true);
  context->device_manager().ActivateDevice(device);

  // Ensure device is plugged, then begin update.
  EXPECT_TRUE(device->plugged());
  bool completed_update = false;
  auto new_profile = ToAudioDeviceTuningProfile(kDefaultPipelineConfig, kVolumeCurve);
  under_test.SetAudioDeviceProfile(kDeviceIdString, std::move(new_profile),
                                   [&completed_update](zx_status_t result) {
                                     completed_update = true;
                                     EXPECT_EQ(ZX_OK, result);
                                   });

  // Verify device has already been removed from RouteGraph in an effort to remove any links
  // during update. Then, unplug device.
  EXPECT_FALSE(context->route_graph().ContainsDevice(device.get()));
  context->device_manager().OnPlugStateChanged(device, false, device->plug_time());
  EXPECT_FALSE(device->plugged());

  // Complete update, and verify device is not added to RouteGraph, since it was unplugged.
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_update);
  EXPECT_FALSE(context->route_graph().ContainsDevice(device.get()));
}

TEST_F(AudioTunerTest, FailSimultaneousPipelineConfigUpdates) {
  auto context = CreateContext();
  AudioTunerImpl under_test(*context);

  // Prepare device to be updated.
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  context->device_manager().ActivateDevice(device);

  bool completed_update1 = false;
  bool completed_update2 = false;
  auto new_profile = ToAudioDeviceTuningProfile(kDefaultPipelineConfig, kVolumeCurve);
  under_test.SetAudioDeviceProfile(kDeviceIdString, fidl::Clone(new_profile),
                                   [&completed_update1](zx_status_t result) {
                                     completed_update1 = true;
                                     EXPECT_EQ(ZX_OK, result);
                                   });
  under_test.SetAudioDeviceProfile(kDeviceIdString, fidl::Clone(new_profile),
                                   [&completed_update2](zx_status_t result) {
                                     completed_update2 = true;
                                     EXPECT_EQ(ZX_ERR_BAD_STATE, result);
                                   });
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_update1);
  EXPECT_TRUE(completed_update2);
}

TEST_F(AudioTunerTest, GetAvailableAudioEffects) {
  auto context = CreateContext();
  AudioTunerImpl under_test(*context);

  // Create an effect we can load.
  test_effects_.AddEffect("test_effect");

  bool received_test_effect = false;
  under_test.GetAvailableAudioEffects(
      [&received_test_effect](std::vector<fuchsia::media::tuning::AudioEffectType> effects) {
        for (size_t i = 0; i < effects.size(); ++i) {
          if (effects[i].module_name() == testing::kTestEffectsModuleName &&
              effects[i].effect_name() == "test_effect") {
            received_test_effect = true;
          }
        }
      });
  EXPECT_TRUE(received_test_effect);
}

TEST_F(AudioTunerTest, InitialGetAudioDeviceProfile) {
  auto expected_process_config =
      ProcessConfigBuilder()
          .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(-60.0))
          .AddDeviceProfile(
              {std::vector<audio_stream_unique_id_t>{kDeviceIdUnique},
               DeviceConfig::OutputDeviceProfile(
                   /*eligible_for_loopback=*/true, /*supported_usages=*/{},
                   /*independent_volume_control=*/false,
                   PipelineConfig(PipelineConfig::MixGroup{
                       .name = "linearize",
                       .input_streams = {RenderUsage::BACKGROUND, RenderUsage::MEDIA},
                       .effects = {PipelineConfig::Effect{.lib_name = "my_effects.so",
                                                          .effect_name = "equalizer",
                                                          .instance_name = "eq1",
                                                          .effect_config = "",
                                                          .output_channels = 2}},
                       .inputs = {PipelineConfig::MixGroup{
                           .name = "mix",
                           .input_streams = {},
                           .effects = {},
                           .inputs = {PipelineConfig::MixGroup{.name = "output_streams",
                                                               .input_streams = {},
                                                               .effects = {},
                                                               .inputs = {},
                                                               .loopback = false,
                                                               .output_rate = 48000,
                                                               .output_channels = 2}},
                           .loopback = false,
                           .output_rate = 48000,
                           .output_channels = 2}},
                       .loopback = true,
                       .output_rate = 48000,
                       .output_channels = 2}))})
          .Build();

  auto context = CreateContext(expected_process_config);
  AudioTunerImpl under_test(*context);
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  context->device_manager().ActivateDevice(device);

  fuchsia::media::tuning::AudioDeviceTuningProfile tuning_profile;
  under_test.GetAudioDeviceProfile(
      kDeviceIdString, [&tuning_profile](fuchsia::media::tuning::AudioDeviceTuningProfile profile) {
        tuning_profile = std::move(profile);
      });

  VolumeCurve expected_curve = expected_process_config.default_volume_curve();
  std::vector<fuchsia::media::tuning::Volume> result_curve = tuning_profile.volume_curve();
  ExpectEq(expected_curve, result_curve);

  PipelineConfig::MixGroup expected_pipeline = expected_process_config.device_config()
                                                   .output_device_profile(kDeviceIdUnique)
                                                   .pipeline_config()
                                                   .root();
  ExpectEq(expected_pipeline, tuning_profile.pipeline());
}

TEST_F(AudioTunerTest, GetDefaultAudioDeviceProfile) {
  auto expected_process_config = kDefaultProcessConfig;
  auto context = CreateContext(expected_process_config);
  AudioTunerImpl under_test(*context);
  fuchsia::media::tuning::AudioDeviceTuningProfile tuning_profile;
  under_test.GetDefaultAudioDeviceProfile(
      kDeviceIdString, [&tuning_profile](fuchsia::media::tuning::AudioDeviceTuningProfile profile) {
        tuning_profile = std::move(profile);
      });

  VolumeCurve expected_curve = expected_process_config.default_volume_curve();
  std::vector<fuchsia::media::tuning::Volume> result_curve = tuning_profile.volume_curve();
  ExpectEq(expected_curve, result_curve);

  PipelineConfig::MixGroup expected_pipeline = expected_process_config.device_config()
                                                   .output_device_profile(kDeviceIdUnique)
                                                   .pipeline_config()
                                                   .root();
  ExpectEq(expected_pipeline, tuning_profile.pipeline());
}

TEST_F(AudioTunerTest, SetGetDeleteAudioDeviceProfile) {
  auto context = CreateContext(kDefaultProcessConfig);
  AudioTunerImpl under_test(*context);

  // Prepare device to be updated.
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  context->device_manager().ActivateDevice(device);

  // Update device with new configuration.
  auto new_pipeline_config = PipelineConfig(
      PipelineConfig::MixGroup{.name = "linearize",
                               .input_streams = {RenderUsage::BACKGROUND, RenderUsage::MEDIA},
                               .effects = {},
                               .inputs = {PipelineConfig::MixGroup{
                                   .name = "mix",
                                   .input_streams = {},
                                   .effects = {},
                                   .inputs = {PipelineConfig::MixGroup{.name = "output_streams",
                                                                       .input_streams = {},
                                                                       .effects = {},
                                                                       .inputs = {},
                                                                       .loopback = false,
                                                                       .output_rate = 48000,
                                                                       .output_channels = 1}},
                                   .loopback = false,
                                   .output_rate = 48000,
                                   .output_channels = 1}},
                               .loopback = true,
                               .output_rate = 96000,
                               .output_channels = 1});
  auto new_profile = ToAudioDeviceTuningProfile(new_pipeline_config, kVolumeCurve);
  bool completed_update = false;
  under_test.SetAudioDeviceProfile(kDeviceIdString, std::move(new_profile),
                                   [&completed_update](zx_status_t result) {
                                     completed_update = true;
                                     EXPECT_EQ(ZX_OK, result);
                                   });
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_update);

  // Verify device configuration was successfully updated.
  fuchsia::media::tuning::AudioDeviceTuningProfile tuning_profile;
  under_test.GetAudioDeviceProfile(
      kDeviceIdString, [&tuning_profile](fuchsia::media::tuning::AudioDeviceTuningProfile profile) {
        tuning_profile = std::move(profile);
      });

  std::vector<fuchsia::media::tuning::Volume> result_curve = tuning_profile.volume_curve();
  ExpectEq(kVolumeCurve, result_curve);
  ExpectEq(new_pipeline_config.root(), tuning_profile.pipeline());

  // Delete tuned device configuration.
  bool completed_delete = false;
  under_test.DeleteAudioDeviceProfile(kDeviceIdString, [&completed_delete](zx_status_t status) {
    completed_delete = true;
    EXPECT_EQ(ZX_OK, status);
  });
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_delete);

  // Verify device configuration was successfully deleted and reset to the default configuration.
  under_test.GetAudioDeviceProfile(
      kDeviceIdString, [&tuning_profile](fuchsia::media::tuning::AudioDeviceTuningProfile profile) {
        tuning_profile = std::move(profile);
      });
  result_curve = tuning_profile.volume_curve();
  ExpectEq(kVolumeCurve, result_curve);
  ExpectEq(kDefaultProcessConfig.device_config()
               .output_device_profile(kDeviceIdUnique)
               .pipeline_config()
               .root(),
           tuning_profile.pipeline());
}

TEST_F(AudioTunerTest, SetAudioEffectConfig) {
  std::string instance_name = "eq1";
  std::string initial_effect_config = "";
  auto initial_process_config =
      ProcessConfigBuilder()
          .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(-60.0))
          .AddDeviceProfile(
              {std::vector<audio_stream_unique_id_t>{kDeviceIdUnique},
               DeviceConfig::OutputDeviceProfile(
                   /*eligible_for_loopback=*/true, /*supported_usages=*/{},
                   /*independent_volume_control=*/false,
                   PipelineConfig(PipelineConfig::MixGroup{
                       .name = "linearize",
                       .input_streams = {RenderUsage::BACKGROUND, RenderUsage::MEDIA},
                       .effects = {PipelineConfig::Effect{.lib_name = "my_effects.so",
                                                          .effect_name = "equalizer",
                                                          .instance_name = instance_name,
                                                          .effect_config = initial_effect_config,
                                                          .output_channels = 2}},
                       .inputs = {PipelineConfig::MixGroup{
                           .name = "mix",
                           .input_streams = {},
                           .effects = {},
                           .inputs = {PipelineConfig::MixGroup{.name = "output_streams",
                                                               .input_streams = {},
                                                               .effects = {},
                                                               .inputs = {},
                                                               .loopback = false,
                                                               .output_rate = 48000,
                                                               .output_channels = 2}},
                           .loopback = false,
                           .output_rate = 48000,
                           .output_channels = 2}},
                       .loopback = true,
                       .output_rate = 48000,
                       .output_channels = 2}))})
          .Build();
  auto context = CreateContext(initial_process_config);
  AudioTunerImpl under_test(*context);

  // Prepare device to be updated.
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  context->device_manager().ActivateDevice(device);

  // Update device with new effect configuration.
  std::string updated_effect_config = "new configuration";
  fuchsia::media::tuning::AudioEffectConfig effect;
  effect.set_instance_name(instance_name);
  effect.set_configuration(updated_effect_config);
  bool completed_update = false;
  under_test.SetAudioEffectConfig(kDeviceIdString, std::move(effect),
                                  [&completed_update](zx_status_t result) {
                                    completed_update = true;
                                    EXPECT_EQ(ZX_OK, result);
                                  });
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_update);

  // Verify device configuration was successfully updated.
  auto expected_pipeline_config = PipelineConfig(PipelineConfig::MixGroup{
      .name = "linearize",
      .input_streams = {RenderUsage::BACKGROUND, RenderUsage::MEDIA},
      .effects = {PipelineConfig::Effect{.lib_name = "my_effects.so",
                                         .effect_name = "equalizer",
                                         .instance_name = instance_name,
                                         .effect_config = updated_effect_config,
                                         .output_channels = 2}},
      .inputs = {PipelineConfig::MixGroup{
          .name = "mix",
          .input_streams = {},
          .effects = {},
          .inputs = {PipelineConfig::MixGroup{.name = "output_streams",
                                              .input_streams = {},
                                              .effects = {},
                                              .inputs = {},
                                              .loopback = false,
                                              .output_rate = 48000,
                                              .output_channels = 2}},
          .loopback = false,
          .output_rate = 48000,
          .output_channels = 2}},
      .loopback = true,
      .output_rate = 48000,
      .output_channels = 2});
  fuchsia::media::tuning::AudioDeviceTuningProfile tuning_profile;
  under_test.GetAudioDeviceProfile(
      kDeviceIdString, [&tuning_profile](fuchsia::media::tuning::AudioDeviceTuningProfile profile) {
        tuning_profile = std::move(profile);
      });
  ExpectEq(expected_pipeline_config.root(), tuning_profile.pipeline());
}

TEST_F(AudioTunerTest, FailSetAudioEffectConfigNoInstanceName) {
  auto context = CreateContext(kDefaultProcessConfig);
  AudioTunerImpl under_test(*context);

  // Prepare device to be updated.
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  context->device_manager().ActivateDevice(device);

  // Attempt device effect update, missing |instance_name|.
  std::string updated_effect_config = "new configuration";
  fuchsia::media::tuning::AudioEffectConfig effect;
  effect.set_configuration(updated_effect_config);
  bool completed_update = false;
  under_test.SetAudioEffectConfig(kDeviceIdString, std::move(effect),
                                  [&completed_update](zx_status_t result) {
                                    completed_update = true;
                                    EXPECT_EQ(ZX_ERR_BAD_STATE, result);
                                  });
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_update);
}

TEST_F(AudioTunerTest, FailSetAudioEffectConfigNoConfig) {
  auto context = CreateContext(kDefaultProcessConfig);
  AudioTunerImpl under_test(*context);

  // Prepare device to be updated.
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  context->device_manager().ActivateDevice(device);

  // Attempt device effect update, missing |configuration|.
  std::string updated_effect_config = "new configuration";
  fuchsia::media::tuning::AudioEffectConfig effect;
  effect.set_instance_name("");
  bool completed_update = false;
  under_test.SetAudioEffectConfig(kDeviceIdString, std::move(effect),
                                  [&completed_update](zx_status_t result) {
                                    completed_update = true;
                                    EXPECT_EQ(ZX_ERR_BAD_STATE, result);
                                  });
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_update);
}

TEST_F(AudioTunerTest, FailSetAudioEffectConfigInvalidInstanceName) {
  auto context = CreateContext(kDefaultProcessConfig);
  AudioTunerImpl under_test(*context);

  // Prepare device to be updated.
  auto device = std::make_shared<TestDevice>(context);
  context->device_manager().AddDevice(device);
  RunLoopUntilIdle();
  context->device_manager().ActivateDevice(device);

  // Attempt device effect update with invalid |instance_name|.
  std::string updated_effect_config = "new configuration";
  fuchsia::media::tuning::AudioEffectConfig effect;
  effect.set_instance_name("invalid_effect");
  effect.set_configuration("new configuration");
  bool completed_update = false;
  under_test.SetAudioEffectConfig(kDeviceIdString, std::move(effect),
                                  [&completed_update](zx_status_t result) {
                                    completed_update = true;
                                    EXPECT_EQ(ZX_ERR_NOT_FOUND, result);
                                  });
  device->CompleteUpdates();
  RunLoopUntilIdle();
  EXPECT_TRUE(completed_update);
}

}  // namespace
}  // namespace media::audio
