// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/audio_device.h"

#include <cstring>
#include <memory>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/shared/device_config.h"
#include "src/media/audio/audio_core/v1/audio_device_manager.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/device_registry.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio {
namespace {

class FakeAudioDevice : public AudioDevice {
 public:
  FakeAudioDevice(AudioDevice::Type type, const DeviceConfig& config,
                  ThreadingModel* threading_model, DeviceRegistry* registry,
                  LinkMatrix* link_matrix, std::shared_ptr<AudioCoreClockFactory> clock_factory)
      : AudioDevice(type, "", config, threading_model, registry, link_matrix, clock_factory,
                    std::make_unique<AudioDriver>(this)) {}

  // Needed because AudioDevice is an abstract class
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       fuchsia::media::AudioGainValidFlags set_flags) override {}
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override {
    driver()->GetDriverInfo();
  }
  void OnDriverInfoFetched() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token()) override {
    driver_info_fetched_ = true;
  }
  bool driver_info_fetched_ = false;
};

class AudioDeviceTest : public testing::ThreadingModelFixture {
 protected:
  static constexpr uint32_t kCustomClockDomain = 42;

  void SetUp() override {
    device_ = std::make_shared<FakeAudioDevice>(
        AudioObject::Type::Input, context().process_config().device_config(), &threading_model(),
        &context().device_manager(), &context().link_matrix(), context().clock_factory());

    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));
    remote_driver_ = std::make_unique<testing::FakeAudioDriver>(std::move(c1), dispatcher());
    remote_driver_->set_clock_domain(kCustomClockDomain);

    device_->driver()->Init(std::move(c2));
    remote_driver_->Start();
  }
  std::shared_ptr<FakeAudioDevice> device_;
  std::unique_ptr<testing::FakeAudioDriver> remote_driver_;
};

// After GetDriverInfo, the clock domain has been set and the ref clock is valid.
TEST_F(AudioDeviceTest, ReferenceClockIsAdvancing) {
  threading_model().FidlDomain().ScheduleTask(device_->Startup());

  RunLoopUntilIdle();
  EXPECT_TRUE(device_->driver_info_fetched_);
  clock::testing::VerifyAdvances(*device_->reference_clock(),
                                 context().clock_factory()->synthetic());
}

TEST_F(AudioDeviceTest, DefaultClockIsClockMono) {
  threading_model().FidlDomain().ScheduleTask(device_->Startup());
  RunLoopUntilIdle();
  EXPECT_TRUE(device_->driver_info_fetched_);

  clock::testing::VerifyIsSystemMonotonic(*device_->reference_clock());
}

}  // namespace
}  // namespace media::audio
