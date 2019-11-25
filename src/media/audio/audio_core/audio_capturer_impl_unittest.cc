// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/audio_capturer_impl.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_input.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/stub_device_registry.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/audio_core/usage_gain_adjustment.h"
#include "src/media/audio/lib/logging/logging.h"

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
        volume_manager_(dispatcher()),
        route_graph_(routing_config_) {
    fzl::VmoMapper vmo_mapper;
    FX_CHECK(vmo_mapper.CreateAndMap(kAudioCapturerUnittestVmarSize,
                                     /*flags=*/0, nullptr, &vmo_) == ZX_OK);
  }

 protected:
  void SetUp() override {
    Logging::Init(-media::audio::SPEW, {"route_graph_test"});
    testing::ThreadingModelFixture::SetUp();

    auto default_curve = VolumeCurve::DefaultForMinGain(-33.0);
    auto process_config = ProcessConfig::Builder().SetDefaultVolumeCurve(default_curve).Build();
    config_handle_ = ProcessConfig::set_instance(process_config);

    route_graph_.SetThrottleOutput(&threading_model(),
                                   ThrottleOutput::Create(&threading_model(), &device_registry_));
    capturer_ = AudioCapturerImpl::Create(
        /*loopback=*/false, fidl_capturer_.NewRequest(), &threading_model(), &route_graph_, &admin_,
        &volume_manager_);
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
  RoutingConfig routing_config_;
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

TEST_F(AudioCapturerImplTest, RegistersWithRouteGraphIfHasUsageStreamTypeAndBuffers) {
  EXPECT_EQ(capturer_->source_link_count(), 0u);

  zx::vmo duplicate;
  ASSERT_EQ(
      vmo_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE | ZX_RIGHT_READ | ZX_RIGHT_MAP, &duplicate),
      ZX_OK);

  zx::channel c1, c2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));
  auto input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  auto fake_driver =
      testing::FakeAudioDriver(std::move(c1), threading_model().FidlDomain().dispatcher());

  input->driver()->Init(std::move(c2));
  fake_driver.Start();
  input->driver()->GetDriverInfo();
  RunLoopUntilIdle();

  input->driver()->Start();
  fake_driver.set_formats({audio_stream_format_range_t{
      .sample_formats = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT,
      .min_frames_per_second = 0,
      .max_frames_per_second = 96000,
      .min_channels = 1,
      .max_channels = 100,
      .flags = 0,
  }});
  route_graph_.AddInput(input.get());
  RunLoopUntilIdle();

  fidl_capturer_->SetPcmStreamType(stream_type_);
  fidl_capturer_->AddPayloadBuffer(0, std::move(duplicate));
  fidl_capturer_->SetUsage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);

  RunLoopUntilIdle();
  EXPECT_EQ(capturer_->source_link_count(), 1u);
}

}  // namespace
}  // namespace media::audio
