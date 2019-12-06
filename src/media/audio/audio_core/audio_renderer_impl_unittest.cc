// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/audio_renderer_impl.h"

#include <lib/fzl/vmar-manager.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/audio_admin.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/stream_volume_manager.h"
#include "src/media/audio/audio_core/testing/fake_audio_device.h"
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

constexpr uint32_t kAudioRendererUnittestFrameRate = 48000;
constexpr size_t kAudioRendererUnittestVmarSize = 16ull * 1024;
constexpr zx_vm_option_t kAudioRendererUnittestVmarFlags =
    ZX_VM_COMPACT | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_1GB;

class AudioRendererImplTest : public testing::ThreadingModelFixture {
 public:
  AudioRendererImplTest()
      : admin_(&gain_adjustment_, dispatcher(), &policy_action_reporter_),
        volume_manager_(dispatcher()),
        route_graph_(routing_config_),
        vmar_(fzl::VmarManager::Create(kAudioRendererUnittestVmarSize, nullptr,
                                       kAudioRendererUnittestVmarFlags)) {
    fzl::VmoMapper vmo_mapper;
    FX_CHECK(vmo_mapper.CreateAndMap(kAudioRendererUnittestVmarSize,
                                     /*flags=*/0, nullptr, &vmo_) == ZX_OK);
  }

 protected:
  void SetUp() override {
    Logging::Init(-media::audio::SPEW, {"audio_core"});
    testing::ThreadingModelFixture::SetUp();

    auto default_curve = VolumeCurve::DefaultForMinGain(-33.0);
    auto process_config = ProcessConfig::Builder().SetDefaultVolumeCurve(default_curve).Build();
    config_handle_ = ProcessConfig::set_instance(process_config);

    route_graph_.SetThrottleOutput(&threading_model(),
                                   ThrottleOutput::Create(&threading_model(), &device_registry_));
    renderer_ = AudioRendererImpl::Create(fidl_renderer_.NewRequest(), dispatcher(), &route_graph_,
                                          &admin_, vmar_, &volume_manager_);
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

  // Creates a new payload buffer of |size| bytes and registers it with the renderer with |id|.
  //
  // A handle to the new VMO is returned.
  zx::vmo AddPayloadBuffer(uint32_t id, size_t size) {
    zx::vmo vmo;
    EXPECT_EQ(ZX_OK, zx::vmo::create(size, 0, &vmo));

    zx::vmo duplicate;
    EXPECT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate));
    renderer_->AddPayloadBuffer(id, std::move(duplicate));
    return vmo;
  }

  void TearDown() override {
    // Dropping the channel queues up a reference to the Renderer through its error handler, which
    // will not work since the rest of this class is destructed before the loop and its
    // queued functions are. Here, we ensure the error handler runs before this class' destructors
    // run.
    { auto r = std::move(fidl_renderer_); }
    RunLoopUntilIdle();

    testing::ThreadingModelFixture::TearDown();
  }

 protected:
  StubUsageGainAdjustment gain_adjustment_;
  StubPolicyActionReporter policy_action_reporter_;
  AudioAdmin admin_;

  testing::StubDeviceRegistry device_registry_;
  StreamVolumeManager volume_manager_;
  RoutingConfig routing_config_;
  RouteGraph route_graph_;

  fuchsia::media::AudioRendererPtr fidl_renderer_;
  fbl::RefPtr<AudioRendererImpl> renderer_;

  zx::vmo vmo_;
  fbl::RefPtr<fzl::VmarManager> vmar_;
  ProcessConfig::Handle config_handle_;

  fuchsia::media::AudioStreamType stream_type_ = {
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 1,
      .frames_per_second = kAudioRendererUnittestFrameRate,
  };
};

constexpr zx::duration kMinLeadTime = zx::nsec(123456789);
constexpr int64_t kInvalidLeadTimeNs = -1;

// Validate that MinLeadTime is provided to AudioRenderer clients accurately
TEST_F(AudioRendererImplTest, MinLeadTimePadding) {
  auto fake_output = testing::FakeAudioOutput::Create(&threading_model(), &device_registry_);

  // We must set our output's lead time, before linking it, before calling SetPcmStreamType().
  fake_output->SetMinLeadTime(kMinLeadTime);

  // Our RouteGraph links one FakeAudioOutput to the Renderer-under-test. Thus we can set our
  // output's MinLeadTime, fully expecting this value to be reflected as-is to renderer+clients.
  route_graph_.AddRenderer(renderer_);
  route_graph_.AddOutput(fake_output.get());

  // SetPcmStreamType triggers the routing preparation completion, which connects output(s) to
  // renderer. Renderers react to new outputs in `OnLinkAdded` by recalculating minimum lead time.
  SetPcmStreamType();

  auto lead_time_ns = kInvalidLeadTimeNs;
  renderer_->GetMinLeadTime(
      [&lead_time_ns](int64_t received_lead_time_ns) { lead_time_ns = received_lead_time_ns; });

  RunLoopUntilIdle();
  ASSERT_NE(lead_time_ns, kInvalidLeadTimeNs) << "No response received for GetMinLeadTime";
  EXPECT_EQ(lead_time_ns, kMinLeadTime.to_nsecs()) << "Incorrect GetMinLeadTime received";
}

TEST_F(AudioRendererImplTest, AllocatePacketQueueForLinks) {
  auto fake_output = testing::FakeAudioOutput::Create(&threading_model(), &device_registry_);

  route_graph_.AddRenderer(renderer_);
  route_graph_.AddOutput(fake_output.get());

  SetPcmStreamType();
  AddPayloadBuffer(0, PAGE_SIZE);
  fuchsia::media::StreamPacket packet;
  packet.payload_buffer_id = 0;
  packet.payload_offset = 0;
  packet.payload_offset = 128;
  renderer_->SendPacketNoReply(std::move(packet));

  ASSERT_EQ(1u, fake_output->source_link_count());
  fake_output->ForEachSourceLink([](auto& link) {
    auto stream = link.stream();
    ASSERT_TRUE(stream);

    {  // Expect a buffer.
      auto buffer = stream->LockBuffer();
      ASSERT_TRUE(buffer);
      EXPECT_FALSE(buffer->is_continuous());
      EXPECT_NE(nullptr, buffer->payload());
      stream->UnlockBuffer(true);
    }
    {  // No more buffers.
      auto buffer = stream->LockBuffer();
      ASSERT_FALSE(buffer);
    }
  });
}

TEST_F(AudioRendererImplTest, RegistersWithRouteGraphIfHasUsageStreamTypeAndBuffers) {
  EXPECT_EQ(renderer_->dest_link_count(), 0u);

  zx::vmo duplicate;
  ASSERT_EQ(
      vmo_.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE | ZX_RIGHT_READ | ZX_RIGHT_MAP, &duplicate),
      ZX_OK);

  auto output = testing::FakeAudioOutput::Create(&threading_model(), &device_registry_);
  route_graph_.AddOutput(output.get());
  RunLoopUntilIdle();

  route_graph_.AddRenderer(renderer_);
  fidl_renderer_->SetPcmStreamType(stream_type_);
  fidl_renderer_->AddPayloadBuffer(0, std::move(duplicate));
  fidl_renderer_->SetUsage(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  RunLoopUntilIdle();
  EXPECT_EQ(renderer_->dest_link_count(), 1u);
}

}  // namespace
}  // namespace media::audio
