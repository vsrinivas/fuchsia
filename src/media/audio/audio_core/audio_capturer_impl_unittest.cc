// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/audio_capturer_impl.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/testing/fake_audio_device.h"
#include "src/media/audio/audio_core/testing/stub_device_registry.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/audio_core/usage_gain_adjustment.h"

namespace media::audio {
namespace {

class StubUsageGainAdjustment : public UsageGainAdjustment {
  void SetRenderUsageGainAdjustment(fuchsia::media::AudioRenderUsage, float) override {}
  void SetCaptureUsageGainAdjustment(fuchsia::media::AudioCaptureUsage, float) override {}
};

class StubPolicyActionReporter : public AudioAdmin::PolicyActionReporter {
  void ReportPolicyAction(fuchsia::media::Usage, fuchsia::media::Behavior) override {}
};

constexpr uint32_t kAudioCapturerUnittestFrameRate = 48000;
constexpr size_t kAudioCapturerUnittestVmarSize = 16ull * 1024;

class AudioCapturerImplTest : public testing::ThreadingModelFixture {
 public:
  AudioCapturerImplTest()
      : admin_(&gain_adjustment_, dispatcher(), &policy_action_reporter_),
        volume_manager_(dispatcher()) {
    fzl::VmoMapper vmo_mapper;
    FX_CHECK(vmo_mapper.CreateAndMap(kAudioCapturerUnittestVmarSize,
                                     /*flags=*/0, nullptr, &vmo_) == ZX_OK);
  }

 protected:
  void SetUp() override {
    testing::ThreadingModelFixture::SetUp();

    auto default_curve = VolumeCurve::DefaultForMinGain(-33.0);
    auto process_config = ProcessConfig::Builder().SetDefaultVolumeCurve(default_curve).Build();
    config_handle_ = ProcessConfig::set_instance(process_config);

    route_graph_.SetThrottleOutput(&threading_model(),
                                   ThrottleOutput::Create(&threading_model(), &device_registry_));
    capturer_ =
        AudioCapturerImpl::Create(/*loopback=*/false, fidl_capturer_.NewRequest(),
                                  &threading_model(), &route_graph_, &admin_, &volume_manager_);
    EXPECT_NE(capturer_.get(), nullptr);

    route_graph_.AddCapturer(capturer_);
  }

  void TearDown() override {
    // Dropping the channel queues up a reference to the Capturer through its error handler, which
    // will not work since the rest of this class is destructed before the loop and its
    // queued functions are. Here, we ensure the error handler runs before this class' destructors
    // run.
    { auto r = std::move(capturer_); }
    { auto r = std::move(fidl_capturer_); }
    RunLoopUntilIdle();

    testing::ThreadingModelFixture::TearDown();
  }

 protected:
  StubUsageGainAdjustment gain_adjustment_;
  StubPolicyActionReporter policy_action_reporter_;
  AudioAdmin admin_;

  testing::StubDeviceRegistry device_registry_;
  StreamVolumeManager volume_manager_;
  RouteGraph route_graph_;

  fuchsia::media::AudioCapturerPtr fidl_capturer_;
  fbl::RefPtr<AudioCapturerImpl> capturer_;

  ProcessConfig::Handle config_handle_;
  zx::vmo vmo_;

  fuchsia::media::AudioStreamType stream_type_ = {
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 1,
      .frames_per_second = kAudioCapturerUnittestFrameRate,
  };
};

TEST_F(AudioCapturerImplTest, CanShutdownWithUnusedBuffer) {
  zx::vmo duplicate;
  ASSERT_EQ(vmo_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE | ZX_RIGHT_READ, &duplicate), ZX_OK);
  fidl_capturer_->SetPcmStreamType(stream_type_);
  fidl_capturer_->AddPayloadBuffer(0, std::move(duplicate));
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace media::audio
