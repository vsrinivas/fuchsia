// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/usage_gain_reporter_impl.h"

#include <lib/fidl/cpp/binding.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

const std::string DEVICE_ID_STRING = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
const audio_stream_unique_id_t DEVICE_ID_AUDIO_STREAM =
    AudioDevice::UniqueIdFromString(DEVICE_ID_STRING).take_value();

const std::string BLUETOOTH_DEVICE_ID_STRING = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const audio_stream_unique_id_t BLUETOOTH_DEVICE_ID_AUDIO_STREAM =
    AudioDevice::UniqueIdFromString(BLUETOOTH_DEVICE_ID_STRING).take_value();

class FakeGainListener : public fuchsia::media::UsageGainListener {
 public:
  fidl::InterfaceHandle<fuchsia::media::UsageGainListener> NewBinding() {
    return binding_.NewBinding();
  }

  bool muted() const { return last_muted_; }

  float gain_dbfs() const { return last_gain_dbfs_; }

  size_t call_count() const { return call_count_; }

 private:
  // |fuchsia::media::UsageGainListener|
  void OnGainMuteChanged(bool muted, float gain_dbfs, OnGainMuteChangedCallback callback) final {
    last_muted_ = muted;
    last_gain_dbfs_ = gain_dbfs;
    call_count_++;
  }

  fidl::Binding<fuchsia::media::UsageGainListener> binding_{this};
  bool last_muted_ = false;
  float last_gain_dbfs_ = 0.0;
  size_t call_count_ = 0;
};

class TestDeviceRegistry : public DeviceRegistry {
 public:
  void AddDeviceInfo(fuchsia::media::AudioDeviceInfo device_info) {
    device_info_.push_back(device_info);
  }

  // |DeviceRegistry|
  void AddDevice(const std::shared_ptr<AudioDevice>& device) final {}
  void ActivateDevice(const std::shared_ptr<AudioDevice>& device) final {}
  void RemoveDevice(const std::shared_ptr<AudioDevice>& device) final {}
  void OnPlugStateChanged(const std::shared_ptr<AudioDevice>& device, bool plugged,
                          zx::time plug_time) final {}
  std::vector<fuchsia::media::AudioDeviceInfo> GetDeviceInfos() final { return device_info_; }

 private:
  std::vector<fuchsia::media::AudioDeviceInfo> device_info_;
};

class UsageGainReporterTest : public gtest::TestLoopFixture {
 protected:
  UsageGainReporterTest()
      : process_config_(
            ProcessConfigBuilder()
                .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(-60.0))
                .AddDeviceProfile({std::vector<audio_stream_unique_id_t>{DEVICE_ID_AUDIO_STREAM},
                                   DeviceConfig::OutputDeviceProfile(
                                       /* eligible_for_loopback=*/true, /*supported_usages=*/{})})
                .AddDeviceProfile(
                    {std::vector<audio_stream_unique_id_t>{BLUETOOTH_DEVICE_ID_AUDIO_STREAM},
                     DeviceConfig::OutputDeviceProfile(/* eligible_for_loopback=*/true,
                                                       /*supported_usages=*/{},
                                                       /* independent_volume_control=*/true)})
                .Build()),
        usage_(fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA)) {}

  std::unique_ptr<FakeGainListener> Listen(std::string device_id) {
    const auto handle = ProcessConfig::set_instance(process_config_);

    auto device_registry = std::make_unique<TestDeviceRegistry>();
    device_registry->AddDeviceInfo({.unique_id = device_id});

    stream_volume_manager_ = std::make_unique<StreamVolumeManager>(dispatcher());
    under_test_ = std::make_unique<UsageGainReporterImpl>(
        *device_registry.get(), *stream_volume_manager_.get(), process_config_);

    auto fake_gain_listener = std::make_unique<FakeGainListener>();
    under_test_->RegisterListener(device_id, fidl::Clone(usage_), fake_gain_listener->NewBinding());

    return fake_gain_listener;
  }

  std::unique_ptr<StreamVolumeManager> stream_volume_manager_;
  std::unique_ptr<UsageGainReporterImpl> under_test_;
  ProcessConfig process_config_;
  const fuchsia::media::Usage usage_;
};

TEST_F(UsageGainReporterTest, UpdatesSingleListenerUsageGain) {
  auto fake_listener = Listen(DEVICE_ID_STRING);
  const float expected_gain_dbfs = -10.0;
  stream_volume_manager_->SetUsageGain(fidl::Clone(usage_), expected_gain_dbfs);

  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(fake_listener->gain_dbfs(), expected_gain_dbfs);
  EXPECT_EQ(fake_listener->call_count(), 2u);
}

TEST_F(UsageGainReporterTest, UpdatesSingleListenerUsageGainAdjustment) {
  auto fake_listener = Listen(DEVICE_ID_STRING);
  const float expected_gain_dbfs = -10.0;
  stream_volume_manager_->SetUsageGainAdjustment(fidl::Clone(usage_), expected_gain_dbfs);

  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(fake_listener->gain_dbfs(), expected_gain_dbfs);
  EXPECT_EQ(fake_listener->call_count(), 2u);
}

TEST_F(UsageGainReporterTest, UpdatesSingleListenerUsageGainCombination) {
  auto fake_listener = Listen(DEVICE_ID_STRING);
  const float expected_gain_dbfs = -10.0;
  stream_volume_manager_->SetUsageGain(fidl::Clone(usage_), expected_gain_dbfs);
  stream_volume_manager_->SetUsageGainAdjustment(fidl::Clone(usage_), expected_gain_dbfs);

  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(fake_listener->gain_dbfs(), (2 * expected_gain_dbfs));
  EXPECT_EQ(fake_listener->call_count(), 3u);
}

TEST_F(UsageGainReporterTest, NoUpdateIndependentVolumeControlSingleListener) {
  auto fake_listener = Listen(BLUETOOTH_DEVICE_ID_STRING);
  const float attempted_gain_dbfs = -10.0;
  stream_volume_manager_->SetUsageGain(fidl::Clone(usage_), attempted_gain_dbfs);

  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(fake_listener->gain_dbfs(), 0.0f);
  EXPECT_EQ(fake_listener->call_count(), 0u);
}

}  // namespace
}  // namespace media::audio
