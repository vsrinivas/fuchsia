// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/audio_renderer_impl.h"

#include <lib/fzl/vmar-manager.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/testing/fake_object_registry.h"
#include "src/media/audio/audio_core/testing/fake_routing.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
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

constexpr uint32_t kAudioRendererUnittestFrameRate = 48000;
constexpr size_t kAudioRendererUnittestVmarSize = 16ull * 1024;
constexpr zx_vm_option_t kAudioRendererUnittestVmarFlags =
    ZX_VM_COMPACT | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_1GB;

class AudioRendererImplTest : public testing::ThreadingModelFixture {
 public:
  AudioRendererImplTest()
      : admin_(&gain_adjustment_, dispatcher(), &policy_action_reporter_),
        volume_manager_(dispatcher()),
        vmar_(fzl::VmarManager::Create(kAudioRendererUnittestVmarSize, nullptr,
                                       kAudioRendererUnittestVmarFlags)) {}

 protected:
  void SetUp() override {
    testing::ThreadingModelFixture::SetUp();

    renderer_ =
        AudioRendererImpl::Create(fidl_renderer_.NewRequest(), dispatcher(), &object_registry_,
                                  &fake_routing_, &admin_, vmar_, &volume_manager_);
    EXPECT_NE(renderer_.get(), nullptr);
  }

  void SetPcmStreamType() {
    fuchsia::media::AudioStreamType stream_type{
        .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
        .channels = 1,
        .frames_per_second = kAudioRendererUnittestFrameRate,
    };

    renderer_->SetPcmStreamType(stream_type);
  }

  void TearDown() override {
    if (renderer_) {
      renderer_->Shutdown();
    }

    auto renderer = std::move(renderer_);

    testing::ThreadingModelFixture::TearDown();
  }

 protected:
  fbl::RefPtr<AudioRendererImpl> renderer_;
  testing::FakeRouting fake_routing_;

  testing::FakeObjectRegistry object_registry_;
  fuchsia::media::AudioRendererPtr fidl_renderer_;

  StubUsageGainAdjustment gain_adjustment_;
  StubPolicyActionReporter policy_action_reporter_;

  AudioAdmin admin_;
  StreamVolumeManager volume_manager_;
  fbl::RefPtr<fzl::VmarManager> vmar_;
};

constexpr zx::duration kMinLeadTime = zx::nsec(123456789);
constexpr zx::duration kTemporaryMinLeadTimePadding = zx::msec(4);
constexpr int64_t kInvalidLeadTimeNs = -1;

// Validate that MinLeadTime is provided to AudioRenderer clients accurately
TEST_F(AudioRendererImplTest, MinLeadTimePadding) {
  // Currently, as short-term workaround, AudioRenderer pads its reported MinLeadTime value.
  zx::duration expected_lead_time = kMinLeadTime + kTemporaryMinLeadTimePadding;

  auto fake_output = testing::FakeAudioOutput::Create(&threading_model(), &object_registry_);

  // We must set our output's lead time, before linking it, before calling SetPcmStreamType().
  fake_output->SetMinClockLeadTime(kMinLeadTime);

  // Our FakeRouting links one FakeAudioOutput to the Renderer-under-test. Thus we can set our
  // output's MinLeadTime, fully expecting this value to be reflected as-is to renderer+clients.
  fake_routing_.AddOutputForRenderer(fake_output);

  // SetPcmStreamType triggers Routing::SelectOutputsForAudioRenderer(), which connects output(s) to
  // renderer then recalculates minimum lead time.
  SetPcmStreamType();

  auto lead_time_ns = kInvalidLeadTimeNs;
  renderer_->GetMinLeadTime(
      [&lead_time_ns](int64_t received_lead_time_ns) { lead_time_ns = received_lead_time_ns; });

  RunLoopUntilIdle();
  ASSERT_NE(lead_time_ns, kInvalidLeadTimeNs) << "No response received for GetMinLeadTime";
  EXPECT_EQ(lead_time_ns, expected_lead_time.to_nsecs()) << "Incorrect GetMinLeadTime received";
}

}  // namespace
}  // namespace media::audio
