// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/route_graph.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/testing/fake_audio_renderer.h"
#include "src/media/audio/audio_core/testing/stub_device_registry.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/throttle_output.h"
#include "src/media/audio/audio_core/usage_settings.h"

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

namespace media::audio {
namespace {

class FakeAudioObject : public AudioObject {
 public:
  static fbl::RefPtr<FakeAudioObject> FakeRenderer() {
    return fbl::AdoptRef(new FakeAudioObject(AudioObject::Type::AudioRenderer));
  }

  static fbl::RefPtr<FakeAudioObject> FakeCapturer() {
    return fbl::AdoptRef(new FakeAudioObject(AudioObject::Type::AudioCapturer));
  }

  FakeAudioObject(AudioObject::Type object_type)
      : AudioObject(object_type),
        format_(Format::Create({.sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8})) {}

  const fbl::RefPtr<Format>& format() const override { return format_; }

  std::vector<AudioObject*> SourceLinks() {
    std::vector<AudioObject*> source_links;
    ForEachSourceLink(
        [&source_links](auto& link) { source_links.push_back(link.GetSource().get()); });
    return source_links;
  }

  std::vector<AudioObject*> DestLinks() {
    std::vector<AudioObject*> dest_links;
    ForEachDestLink([&dest_links](auto& link) { dest_links.push_back(link.GetDest().get()); });
    return dest_links;
  }

 private:
  fbl::RefPtr<Format> format_;
};

// TODO(39532): Remove; use a real output class with fake hardware.
class FakeAudioOutput : public AudioOutput {
 public:
  static fbl::RefPtr<FakeAudioOutput> Create(ThreadingModel* threading_model,
                                             testing::StubDeviceRegistry* device_registry) {
    return fbl::AdoptRef(new FakeAudioOutput(threading_model, device_registry));
  }

  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) override {}
  void OnWakeup() override {}

  bool StartMixJob(MixJob* job, zx::time process_start) override { return true; }
  bool FinishMixJob(const MixJob& job) override { return true; }

 private:
  FakeAudioOutput(ThreadingModel* threading_model, testing::StubDeviceRegistry* device_registry)
      : AudioOutput(threading_model, device_registry) {}
};

class RouteGraphTest : public testing::ThreadingModelFixture {
 public:
  RouteGraphTest()
      : under_test_(routing_config_),
        throttle_output_(ThrottleOutput::Create(&threading_model(), &device_registry_)) {
    under_test_.SetThrottleOutput(&threading_model(), throttle_output_);
  }

  testing::StubDeviceRegistry device_registry_;
  RoutingConfig routing_config_;
  RouteGraph under_test_;
  fbl::RefPtr<AudioOutput> throttle_output_;
};

TEST_F(RouteGraphTest, RenderersAreUnlinkedWhenHaveNoRoutingProfile) {
  auto renderer = FakeAudioObject::FakeRenderer();
  under_test_.AddRenderer(renderer);
  EXPECT_THAT(renderer->DestLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, RenderersRouteToLastPluggedOutput) {
  auto renderer = FakeAudioObject::FakeRenderer();
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      renderer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(first_output.get());
  EXPECT_THAT(renderer->DestLinks(), UnorderedElementsAreArray({first_output.get()}));

  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(later_output.get());
  EXPECT_THAT(renderer->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{later_output.get()}));
}

TEST_F(RouteGraphTest, RenderersFallbackWhenOutputRemoved) {
  auto renderer = FakeAudioObject::FakeRenderer();
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      renderer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);

  under_test_.AddOutput(first_output.get());
  under_test_.AddOutput(later_output.get());

  under_test_.RemoveOutput(later_output.get());
  EXPECT_THAT(renderer->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{first_output.get()}));

  under_test_.RemoveOutput(first_output.get());
  EXPECT_THAT(renderer->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{throttle_output_.get()}));
}

TEST_F(RouteGraphTest, RemovingNonLastOutputDoesNotRerouteRenderers) {
  auto renderer = FakeAudioObject::FakeRenderer();
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      renderer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto second_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto last_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);

  under_test_.AddOutput(first_output.get());
  under_test_.AddOutput(second_output.get());
  under_test_.AddOutput(last_output.get());

  under_test_.RemoveOutput(second_output.get());
  EXPECT_THAT(renderer->DestLinks(), UnorderedElementsAreArray({last_output.get()}));

  under_test_.RemoveOutput(first_output.get());
  EXPECT_THAT(renderer->DestLinks(), UnorderedElementsAreArray({last_output.get()}));
}

TEST_F(RouteGraphTest, RenderersPickUpLastPluggedOutputWhenRoutable) {
  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(first_output.get());

  auto renderer = FakeAudioObject::FakeRenderer();
  under_test_.AddRenderer(renderer);
  EXPECT_THAT(renderer->DestLinks(), IsEmpty());

  under_test_.SetRendererRoutingProfile(
      renderer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  EXPECT_THAT(renderer->DestLinks(), UnorderedElementsAreArray({first_output.get()}));
}

TEST_F(RouteGraphTest, RenderersAreRemoved) {
  auto renderer = FakeAudioObject::FakeRenderer();
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      renderer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});

  // Now the renderer should have 3 references:
  //   1. Ours in this test.
  //   2. The RouteGraph's.
  //   3. The ThrottleOutput's (because they are linked).
  EXPECT_THAT(renderer->DestLinks(), UnorderedElementsAreArray({throttle_output_.get()}));
  EXPECT_EQ(renderer->ref_count_debug(), 3);

  under_test_.RemoveRenderer(renderer.get());
  EXPECT_EQ(renderer->ref_count_debug(), 1);
}

TEST_F(RouteGraphTest, CapturersAreUnlinkedWhenHaveNoRoutingProfile) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddCapturer(capturer);
  EXPECT_THAT(capturer->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, CapturersRouteToLastPluggedInput) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(first_input.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({first_input.get()}));

  auto later_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(later_input.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({later_input.get()}));
}

TEST_F(RouteGraphTest, CapturersFallbackWhenInputRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  auto later_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);

  under_test_.AddInput(first_input.get());
  under_test_.AddInput(later_input.get());

  under_test_.RemoveInput(later_input.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({first_input.get()}));

  under_test_.RemoveInput(first_input.get());
  EXPECT_THAT(capturer->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, RemovingNonLastInputDoesNotRerouteCapturers) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  auto second_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  auto last_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);

  under_test_.AddInput(first_input.get());
  under_test_.AddInput(second_input.get());
  under_test_.AddInput(last_input.get());

  under_test_.RemoveInput(first_input.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({last_input.get()}));

  under_test_.RemoveInput(second_input.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({last_input.get()}));
}

TEST_F(RouteGraphTest, CapturersPickUpLastPluggedInputWhenRoutable) {
  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(first_input.get());

  auto later_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(later_input.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddCapturer(capturer);
  EXPECT_THAT(capturer->SourceLinks(), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({later_input.get()}));
}

TEST_F(RouteGraphTest, CapturersAreRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_EQ(capturer->ref_count_debug(), 2);
  under_test_.RemoveCapturer(capturer.get());
  EXPECT_EQ(capturer->ref_count_debug(), 1);
}

TEST_F(RouteGraphTest, LoopbackCapturersAreUnlinkedWhenHaveNoRoutingProfile) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddLoopbackCapturer(capturer);
  EXPECT_THAT(capturer->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, LoopbackCapturersRouteToLastPluggedOutput) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddLoopbackCapturer(capturer);
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(first_output.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({first_output.get()}));

  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(later_output.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({later_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersFallbackWhenOutputRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddLoopbackCapturer(capturer);
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);

  under_test_.AddOutput(first_output.get());
  under_test_.AddOutput(later_output.get());

  under_test_.RemoveOutput(later_output.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({first_output.get()}));

  under_test_.RemoveOutput(first_output.get());
  EXPECT_THAT(capturer->SourceLinks(), IsEmpty());
}

TEST_F(RouteGraphTest, RemovingNonLastOutputDoesNotRerouteLoopbackCapturers) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddLoopbackCapturer(capturer);
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto second_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  auto last_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);

  under_test_.AddOutput(first_output.get());
  under_test_.AddOutput(second_output.get());
  under_test_.AddOutput(last_output.get());

  under_test_.RemoveOutput(second_output.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({last_output.get()}));

  under_test_.RemoveOutput(first_output.get());
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({last_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersPickUpLastPluggedOutputWhenRoutable) {
  auto first_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(first_output.get());

  auto later_output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(later_output.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddLoopbackCapturer(capturer);
  EXPECT_THAT(capturer->SourceLinks(), IsEmpty());

  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({later_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersAreRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddLoopbackCapturer(capturer);
  EXPECT_EQ(capturer->ref_count_debug(), 2);
  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_EQ(capturer->ref_count_debug(), 2);
  under_test_.RemoveLoopbackCapturer(capturer.get());
  EXPECT_EQ(capturer->ref_count_debug(), 1);
}

TEST_F(RouteGraphTest, OutputRouteCategoriesDoNotAffectEachOther) {
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddLoopbackCapturer(capturer);
  EXPECT_THAT(capturer->SourceLinks(), IsEmpty());

  auto renderer = FakeAudioObject::FakeRenderer();
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      renderer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  EXPECT_THAT(capturer->SourceLinks(), IsEmpty());
  EXPECT_THAT(renderer->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));

  under_test_.SetLoopbackCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({output.get()}));
  EXPECT_THAT(renderer->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));
}

TEST_F(RouteGraphTest, InputRouteCategoriesDoNotAffectOutputs) {
  auto output = FakeAudioOutput::Create(&threading_model(), &device_registry_);
  under_test_.AddOutput(output.get());

  auto first_input = AudioInput::Create(zx::channel(), &threading_model(), &device_registry_);
  under_test_.AddInput(first_input.get());

  auto capturer = FakeAudioObject::FakeCapturer();
  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      capturer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({first_input.get()}));

  auto renderer = FakeAudioObject::FakeRenderer();
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      renderer.get(),
      {.routable = true, .usage = UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA)});
  EXPECT_THAT(renderer->DestLinks(),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));
  EXPECT_THAT(capturer->SourceLinks(), UnorderedElementsAreArray({first_input.get()}));
}

}  // namespace
}  // namespace media::audio
