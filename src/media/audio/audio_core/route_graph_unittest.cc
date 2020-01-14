// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/route_graph.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/fake_audio_renderer.h"
#include "src/media/audio/audio_core/testing/stub_device_registry.h"
#include "src/media/audio/audio_core/testing/test_process_config.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/audio_core/usage_settings.h"
#include "src/media/audio/lib/logging/logging.h"

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

namespace media::audio {
namespace {

class FakeAudioObject : public AudioObject {
 public:
  static std::unique_ptr<FakeAudioObject> FakeRenderer(
      bool valid_format = true,
      fuchsia::media::AudioRenderUsage usage = fuchsia::media::AudioRenderUsage::MEDIA) {
    return std::make_unique<FakeAudioObject>(AudioObject::Type::AudioRenderer, valid_format,
                                             UsageFrom(usage));
  }

  static std::unique_ptr<FakeAudioObject> FakeCapturer(
      fuchsia::media::AudioCaptureUsage usage = fuchsia::media::AudioCaptureUsage::FOREGROUND) {
    return std::make_unique<FakeAudioObject>(AudioObject::Type::AudioCapturer,
                                             /*valid_format=*/true, UsageFrom(usage));
  }

  FakeAudioObject(AudioObject::Type object_type, bool valid_format, fuchsia::media::Usage usage)
      : AudioObject(object_type) {
    if (valid_format) {
      format_ = Format::Create({.sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8});
    }
    usage_ = std::move(usage);
  }

  const fbl::RefPtr<Format>& format() const override { return format_; }

  std::optional<fuchsia::media::Usage> usage() const override { return {fidl::Clone(usage_)}; }

  std::vector<const AudioObject*> SourceLinks() {
    std::vector<const AudioObject*> source_links;
    ForEachSourceLink(
        [&source_links](AudioLink& link) { source_links.push_back(&link.GetSource()); });
    return source_links;
  }

  std::vector<const AudioObject*> DestLinks() {
    std::vector<const AudioObject*> dest_links;
    ForEachDestLink([&dest_links](AudioLink& link) { dest_links.push_back(&link.GetDest()); });
    return dest_links;
  }

 private:
  fbl::RefPtr<Format> format_ = nullptr;
  fuchsia::media::Usage usage_;
};

// TODO(39532): Remove; use a real output class with fake hardware.
class FakeAudioOutput : public AudioOutput {
 public:
  static std::shared_ptr<FakeAudioOutput> Create(ThreadingModel* threading_model,
                                                 testing::StubDeviceRegistry* device_registry) {
    return std::make_shared<FakeAudioOutput>(threading_model, device_registry);
  }

  FakeAudioOutput(ThreadingModel* threading_model, testing::StubDeviceRegistry* device_registry)
      : AudioOutput(threading_model, device_registry) {}

  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) override {}
  void OnWakeup() override {}

  std::optional<MixStage::FrameSpan> StartMixJob(zx::time process_start) override {
    return std::nullopt;
  }

  std::vector<const AudioObject*> SourceLinks() {
    std::vector<const AudioObject*> source_links;
    ForEachSourceLink(
        [&source_links](AudioLink& link) { source_links.push_back(&link.GetSource()); });
    return source_links;
  }

  std::vector<const AudioObject*> DestLinks() {
    std::vector<const AudioObject*> dest_links;
    ForEachDestLink([&dest_links](AudioLink& link) { dest_links.push_back(&link.GetDest()); });
    return dest_links;
  }

  void FinishMixJob(const MixStage::FrameSpan& span, float* buffer) override {}
};

static const RoutingConfig kConfigNoPolicy = RoutingConfig();

class RouteGraphTest : public testing::ThreadingModelFixture {
 public:
  RouteGraphTest() : RouteGraphTest(kConfigNoPolicy) {}

  RouteGraphTest(const RoutingConfig& routing_config)
      : under_test_(routing_config),
        throttle_output_(ThrottleOutput::Create(&threading_model(), &device_registry_)) {
    Logging::Init(-media::audio::SPEW, {"route_graph_test"});
    under_test_.SetThrottleOutput(&threading_model(), throttle_output_);
  }

  struct FakeOutputAndDriver {
    std::shared_ptr<FakeAudioOutput> output;
    std::unique_ptr<testing::FakeAudioDriver> fake_driver;
  };

  FakeOutputAndDriver OutputWithDeviceId(const audio_stream_unique_id_t& device_id) {
    auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
    zx::channel c1, c2;
    ZX_ASSERT(ZX_OK == zx::channel::create(0, &c1, &c2));
    auto fake_driver = std::make_unique<testing::FakeAudioDriver>(
        std::move(c1), threading_model().FidlDomain().dispatcher());
    fake_driver->set_stream_unique_id(device_id);
    ZX_ASSERT(ZX_OK == output->driver()->Init(std::move(c2)));

    fake_driver->Start();
    output->driver()->GetDriverInfo();
    RunLoopUntilIdle();

    return {output, std::move(fake_driver)};
  }

  testing::TestProcessConfig process_config_;
  testing::StubDeviceRegistry device_registry_;
  RouteGraph under_test_;
  std::shared_ptr<AudioOutput> throttle_output_;
};

TEST_F(RouteGraphTest, RenderersAreUnlinkedWhenHaveNoRoutingProfile) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  EXPECT_THAT(renderer_raw->DestLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, RenderersRouteToLastPluggedOutput) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(first_output.get());
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({first_output.get()}));

  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(later_output.get());
  EXPECT_THAT(renderer_raw->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{later_output.get()}));
}

TEST_F(RouteGraphTest, RenderersFallbackWhenOutputRemoved) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);

  under_test_.AddOutput(first_output.get());
  under_test_.AddOutput(later_output.get());

  under_test_.RemoveOutput(later_output.get());
  EXPECT_THAT(renderer_raw->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{first_output.get()}));

  under_test_.RemoveOutput(first_output.get());
  EXPECT_THAT(renderer_raw->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{throttle_output_.get()}));
}

TEST_F(RouteGraphTest, RemovingNonLastOutputDoesNotRerouteRenderers) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto second_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto last_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);

  under_test_.AddOutput(first_output.get());
  under_test_.AddOutput(second_output.get());
  under_test_.AddOutput(last_output.get());

  under_test_.RemoveOutput(second_output.get());
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({last_output.get()}));

  under_test_.RemoveOutput(first_output.get());
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({last_output.get()}));
}

TEST_F(RouteGraphTest, RenderersPickUpLastPluggedOutputWhenRoutable) {
  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(first_output.get());

  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  EXPECT_THAT(renderer_raw->DestLinks(), IsEmpty());

  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({first_output.get()}));
}

TEST_F(RouteGraphTest, RenderersAreRemoved) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  // Now the renderer should have 3 references:
  //   1. Ours in this test.
  //   2. The RouteGraph's.
  //   3. The ThrottleOutput's (because they are linked).
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({throttle_output_.get()}));

  under_test_.RemoveRenderer(renderer_raw);
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());
  EXPECT_THAT(output->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, CapturersAreUnlinkedWhenHaveNoRoutingProfile) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, CapturersRouteToLastPluggedInput) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(first_input.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({first_input.get()}));

  auto later_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(later_input.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({later_input.get()}));
}

TEST_F(RouteGraphTest, CapturersFallbackWhenInputRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  auto later_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);

  under_test_.AddInput(first_input.get());
  under_test_.AddInput(later_input.get());

  under_test_.RemoveInput(later_input.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({first_input.get()}));

  under_test_.RemoveInput(first_input.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, RemovingNonLastInputDoesNotRerouteCapturers) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  auto second_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  auto last_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);

  under_test_.AddInput(first_input.get());
  under_test_.AddInput(second_input.get());
  under_test_.AddInput(last_input.get());

  under_test_.RemoveInput(first_input.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({last_input.get()}));

  under_test_.RemoveInput(second_input.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({last_input.get()}));
}

TEST_F(RouteGraphTest, CapturersPickUpLastPluggedInputWhenRoutable) {
  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(first_input.get());

  auto later_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(later_input.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({later_input.get()}));
}

TEST_F(RouteGraphTest, CapturersAreRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto input = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddInput(input.get());
  EXPECT_THAT(input->DestLinks(), UnorderedElementsAreArray({capturer_raw}));

  under_test_.RemoveLoopbackCapturer(capturer_raw);
  EXPECT_THAT(input->DestLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, LoopbackCapturersAreUnlinkedWhenHaveNoRoutingProfile) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, LoopbackCapturersRouteToLastPluggedOutput) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(first_output.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({first_output.get()}));

  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(later_output.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({later_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersFallbackWhenOutputRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);

  under_test_.AddOutput(first_output.get());
  under_test_.AddOutput(later_output.get());

  under_test_.RemoveOutput(later_output.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({first_output.get()}));

  under_test_.RemoveOutput(first_output.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, RemovingNonLastOutputDoesNotRerouteLoopbackCapturers) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto second_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto last_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);

  under_test_.AddOutput(first_output.get());
  under_test_.AddOutput(second_output.get());
  under_test_.AddOutput(last_output.get());

  under_test_.RemoveOutput(second_output.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({last_output.get()}));

  under_test_.RemoveOutput(first_output.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({last_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersPickUpLastPluggedOutputWhenRoutable) {
  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(first_output.get());

  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(later_output.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());

  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({later_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersAreRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());
  EXPECT_THAT(output->DestLinks(), UnorderedElementsAreArray({capturer_raw}));

  under_test_.RemoveLoopbackCapturer(capturer_raw);
  EXPECT_THAT(output->DestLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, OutputRouteCategoriesDoNotAffectEachOther) {
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());

  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());
  EXPECT_THAT(renderer_raw->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));

  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({output.get()}));
  EXPECT_THAT(renderer_raw->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));
}

TEST_F(RouteGraphTest, InputRouteCategoriesDoNotAffectOutputs) {
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());

  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(first_input.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({first_input.get()}));

  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  EXPECT_THAT(renderer_raw->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({first_input.get()}));
}

TEST_F(RouteGraphTest, DoesNotRouteUnroutableRenderer) {
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());

  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  EXPECT_THAT(renderer_raw->DestLinks(), IsEmpty());

  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = false, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  auto second_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(second_output.get());
  EXPECT_THAT(renderer_raw->DestLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, DoesNotRouteUnroutableCapturer) {
  auto input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(input.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = false, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto second_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(second_input.get());
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, DoesNotRouteUnroutableLoopbackCapturer) {
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());

  auto loopback_capturer = FakeAudioObject::FakeCapturer();
  auto* loopback_capturer_raw = loopback_capturer.get();

  under_test_.AddLoopbackCapturer(std::move(loopback_capturer));
  EXPECT_THAT(loopback_capturer_raw->SourceLinks(), IsEmpty());

  under_test_.SetLoopbackCapturerRoutingProfile(
      loopback_capturer_raw,
      {.routable = false, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto second_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(second_output.get());
  EXPECT_THAT(loopback_capturer_raw->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, AcceptsUnroutableRendererWithInvalidFormat) {
  auto renderer = FakeAudioObject::FakeRenderer(/*valid_format=*/false);
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = false, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  // Passes by not crashing.
}

TEST_F(RouteGraphTest, UnroutesNewlyUnRoutableRenderer) {
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());

  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  EXPECT_THAT(renderer_raw->DestLinks(), IsEmpty());

  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = false, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  EXPECT_THAT(renderer_raw->DestLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, UnroutesNewlyUnRoutableCapturer) {
  auto input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(input.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddCapturer(std::move(capturer));
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  under_test_.SetCapturerRoutingProfile(
      capturer_raw,
      {.routable = false, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer_raw->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, UnroutesNewlyUnRoutableLoopbackCapturer) {
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());

  auto loopback_capturer = FakeAudioObject::FakeCapturer();
  auto* loopback_capturer_raw = loopback_capturer.get();

  under_test_.AddLoopbackCapturer(std::move(loopback_capturer));
  EXPECT_THAT(loopback_capturer_raw->SourceLinks(), IsEmpty());

  under_test_.SetLoopbackCapturerRoutingProfile(
      loopback_capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  under_test_.SetLoopbackCapturerRoutingProfile(
      loopback_capturer_raw,
      {.routable = false, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(loopback_capturer_raw->SourceLinks(), IsEmpty());
}

const audio_stream_unique_id_t kSupportsAllDeviceId = audio_stream_unique_id_t{.data = {0x33}};
const audio_stream_unique_id_t kUnconfiguredDeviceId = audio_stream_unique_id_t{.data = {0x45}};

static const RoutingConfig kConfigWithMediaExternalRoutingPolicy = RoutingConfig(
    /*profiles=*/{{kSupportsAllDeviceId,
                   RoutingConfig::DeviceProfile(
                       /*eligible_for_loopback=*/true,
                       /*output_usage_support_set=*/
                       {fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND),
                        fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA),
                        fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::INTERRUPTION),
                        fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT),
                        fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::COMMUNICATION)})}},
    /*default=*/{RoutingConfig::DeviceProfile(
        /*eligible_for_loopback=*/true, /*output_usage_support_set=*/{
            fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA)})});

class RouteGraphWithMediaExternalPolicyTest : public RouteGraphTest {
 public:
  RouteGraphWithMediaExternalPolicyTest() : RouteGraphTest(kConfigWithMediaExternalRoutingPolicy) {}
};

TEST_F(RouteGraphWithMediaExternalPolicyTest, MediaRoutesToLastPluggedSupportedDevice) {
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  auto output = output_and_driver.output.get();
  under_test_.AddOutput(output);

  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({output}));

  auto unconfigured_output_and_driver = OutputWithDeviceId(kUnconfiguredDeviceId);
  auto unconfigured_output = unconfigured_output_and_driver.output.get();
  under_test_.AddOutput(unconfigured_output);
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({unconfigured_output}));
}

TEST_F(RouteGraphWithMediaExternalPolicyTest, InterruptionDoesNotRouteToUnsupportedDevice) {
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  auto output = output_and_driver.output.get();
  under_test_.AddOutput(output);

  auto renderer = FakeAudioObject::FakeRenderer();
  auto* renderer_raw = renderer.get();

  under_test_.AddRenderer(std::move(renderer));
  under_test_.SetRendererRoutingProfile(
      renderer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::INTERRUPTION)});
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({output}));

  auto unconfigured_output_and_driver = OutputWithDeviceId(kUnconfiguredDeviceId);
  auto unconfigured_output = unconfigured_output_and_driver.output.get();
  under_test_.AddOutput(unconfigured_output);
  EXPECT_THAT(renderer_raw->DestLinks(), UnorderedElementsAreArray({output}));
}

const audio_stream_unique_id_t kSupportsLoopbackDeviceId = audio_stream_unique_id_t{.data = {0x7a}};

static const RoutingConfig kConfigWithExternNonLoopbackDevicePolicy = RoutingConfig(
    /*profiles=*/{{kSupportsAllDeviceId,
                   RoutingConfig::DeviceProfile(
                       /*eligible_for_loopback=*/true,
                       /*output_usage_support_set=*/
                       {fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND),
                        fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA),
                        fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::INTERRUPTION),
                        fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT),
                        fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::COMMUNICATION)})},
                  {kSupportsLoopbackDeviceId,
                   RoutingConfig::DeviceProfile(
                       /*eligible_for_loopback=*/true,
                       /*output_usage_support_set=*/
                       {fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND)})}},
    /*default=*/{RoutingConfig::DeviceProfile(
        /*eligible_for_loopback=*/false, /*output_usage_support_set=*/{
            fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::BACKGROUND),
            fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::MEDIA),
            fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::INTERRUPTION),
            fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT),
            fidl::ToUnderlying(fuchsia::media::AudioRenderUsage::COMMUNICATION)})});

class RouteGraphWithExternalNonLoopbackDeviceTest : public RouteGraphTest {
 public:
  RouteGraphWithExternalNonLoopbackDeviceTest()
      : RouteGraphTest(kConfigWithExternNonLoopbackDevicePolicy) {}
};

TEST_F(RouteGraphWithExternalNonLoopbackDeviceTest, LoopbackRoutesToLastPluggedSupported) {
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  auto output = output_and_driver.output.get();
  under_test_.AddOutput(output);

  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({output}));

  auto second_output_and_driver = OutputWithDeviceId(kSupportsLoopbackDeviceId);
  auto second_output = second_output_and_driver.output.get();
  under_test_.AddOutput(second_output);
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({second_output}));
}

TEST_F(RouteGraphWithExternalNonLoopbackDeviceTest, LoopbackDoesNotRouteToUnsupportedDevice) {
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  auto output = output_and_driver.output.get();
  under_test_.AddOutput(output);

  auto capturer = FakeAudioObject::FakeCapturer();
  auto* capturer_raw = capturer.get();

  under_test_.AddLoopbackCapturer(std::move(capturer));
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer_raw,
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({output}));

  auto second_output_and_driver = OutputWithDeviceId(kUnconfiguredDeviceId);
  auto second_output = second_output_and_driver.output.get();
  under_test_.AddOutput(second_output);
  EXPECT_THAT(capturer_raw->SourceLinks(), UnorderedElementsAreArray({output}));
}

}  // namespace
}  // namespace media::audio
