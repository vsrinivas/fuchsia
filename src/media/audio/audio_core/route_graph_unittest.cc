// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/route_graph.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/testing/fake_audio_renderer.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/usage_settings.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/logging/logging.h"

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

namespace media::audio {
namespace {

class FakeAudioObject : public AudioObject {
 public:
  static std::shared_ptr<FakeAudioObject> FakeRenderer(bool valid_format = true,
                                                       RenderUsage usage = RenderUsage::MEDIA) {
    return std::make_shared<FakeAudioObject>(AudioObject::Type::AudioRenderer, valid_format,
                                             StreamUsage::WithRenderUsage(std::move(usage)));
  }

  static std::shared_ptr<FakeAudioObject> FakeCapturer(
      CaptureUsage usage = CaptureUsage::FOREGROUND) {
    return std::make_shared<FakeAudioObject>(AudioObject::Type::AudioCapturer,
                                             /*valid_format=*/true,
                                             StreamUsage::WithCaptureUsage(std::move(usage)));
  }

  FakeAudioObject(AudioObject::Type object_type, bool valid_format, StreamUsage usage)
      : AudioObject(object_type) {
    if (valid_format) {
      auto format_result = Format::Create({
          .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
          .channels = 1,
          .frames_per_second = 48000,
      });
      format_ = {format_result.take_value()};
    }
    usage_ = std::move(usage);
  }

  std::optional<Format> format() const override { return format_; }
  std::optional<StreamUsage> usage() const override { return {usage_}; }
  void OnLinkAdded() override { total_links_formed_++; }

  uint32_t total_links_formed() { return total_links_formed_; }

 private:
  std::optional<Format> format_;
  StreamUsage usage_;
  uint32_t total_links_formed_ = 0;
};

// TODO(fxbug.dev/39532): Remove; use a real output class with fake hardware.
class FakeAudioOutput : public AudioOutput {
 public:
  static std::shared_ptr<FakeAudioOutput> Create(ThreadingModel* threading_model,
                                                 DeviceRegistry* device_registry,
                                                 LinkMatrix* link_matrix) {
    return std::make_shared<FakeAudioOutput>(threading_model, device_registry, link_matrix);
  }

  FakeAudioOutput(ThreadingModel* threading_model, DeviceRegistry* device_registry,
                  LinkMatrix* link_matrix)
      : AudioOutput("", threading_model, device_registry, link_matrix) {}

  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       fuchsia::media::AudioGainValidFlags set_flags) override {}
  void OnWakeup() override {}

  std::optional<AudioOutput::FrameSpan> StartMixJob(zx::time ref_time) override {
    return std::nullopt;
  }
  void FinishMixJob(const AudioOutput::FrameSpan& span, float* buffer) override {}
  zx::duration MixDeadline() const override { return zx::msec(10); }

  fit::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) override {
    return fit::ok(nullptr);
  }

  void set_format(Format format) { format_ = format; }
  std::optional<Format> format() const override { return format_; }

 private:
  std::optional<Format> format_;
};

static const DeviceConfig kConfigNoPolicy = DeviceConfig();

class RouteGraphTest : public testing::ThreadingModelFixture {
 public:
  RouteGraphTest() : RouteGraphTest(kConfigNoPolicy) {}

  RouteGraphTest(const DeviceConfig& device_config)
      : ThreadingModelFixture(
            ProcessConfig(VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
                          device_config, ThermalConfig({}))) {}

  struct FakeOutputAndDriver {
    std::shared_ptr<FakeAudioOutput> output;
    std::unique_ptr<testing::FakeAudioDriverV1> fake_driver;
  };

  FakeOutputAndDriver OutputWithDeviceId(const audio_stream_unique_id_t& device_id) {
    auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                          &context().link_matrix());
    zx::channel c1, c2;
    ZX_ASSERT(ZX_OK == zx::channel::create(0, &c1, &c2));
    auto fake_driver = std::make_unique<testing::FakeAudioDriverV1>(
        std::move(c1), threading_model().FidlDomain().dispatcher());
    fake_driver->set_stream_unique_id(device_id);
    ZX_ASSERT(ZX_OK == output->driver()->Init(std::move(c2)));

    fake_driver->Start();
    output->driver()->GetDriverInfo();
    RunLoopUntilIdle();

    return {output, std::move(fake_driver)};
  }

  std::vector<AudioObject*> SourceLinks(const AudioObject& object) {
    std::vector<LinkMatrix::LinkHandle> handles;
    context().link_matrix().SourceLinks(object, &handles);

    std::vector<AudioObject*> links;
    std::transform(handles.begin(), handles.end(), std::back_inserter(links),
                   [](auto handle) { return handle.object.get(); });
    return links;
  }

  std::vector<AudioObject*> DestLinks(const AudioObject& object) {
    std::vector<LinkMatrix::LinkHandle> handles;
    context().link_matrix().DestLinks(object, &handles);

    std::vector<AudioObject*> links;
    std::transform(handles.begin(), handles.end(), std::back_inserter(links),
                   [](auto handle) { return handle.object.get(); });
    return links;
  }

  RouteGraph& under_test_ = context().route_graph();
};

TEST_F(RouteGraphTest, RenderersAreUnlinkedWhenHaveNoRoutingProfile) {
  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  EXPECT_THAT(DestLinks(*renderer), IsEmpty());
}

TEST_F(RouteGraphTest, RenderersRouteToLastPluggedOutput) {
  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  under_test_.AddDevice(first_output.get());
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({first_output.get()}));

  auto later_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  under_test_.AddDevice(later_output.get());
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({later_output.get()}));
}

TEST_F(RouteGraphTest, RenderersFallbackWhenOutputRemoved) {
  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  auto later_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());

  under_test_.AddDevice(first_output.get());
  under_test_.AddDevice(later_output.get());

  under_test_.RemoveDevice(later_output.get());
  EXPECT_THAT(DestLinks(*renderer),
              UnorderedElementsAreArray(std::vector<AudioObject*>{first_output.get()}));

  under_test_.RemoveDevice(first_output.get());
  EXPECT_THAT(DestLinks(*renderer),
              UnorderedElementsAreArray(std::vector<AudioObject*>{context().throttle_output()}));
}

TEST_F(RouteGraphTest, RemovingNonLastOutputDoesNotRerouteRenderers) {
  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  auto second_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                               &context().link_matrix());
  auto last_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                             &context().link_matrix());

  under_test_.AddDevice(first_output.get());
  under_test_.AddDevice(second_output.get());
  under_test_.AddDevice(last_output.get());

  under_test_.RemoveDevice(second_output.get());
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({last_output.get()}));

  under_test_.RemoveDevice(first_output.get());
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({last_output.get()}));
}

TEST_F(RouteGraphTest, RenderersPickUpLastPluggedOutputWhenRoutable) {
  auto first_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  under_test_.AddDevice(first_output.get());

  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  EXPECT_THAT(DestLinks(*renderer), IsEmpty());

  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({first_output.get()}));
}

TEST_F(RouteGraphTest, RenderersAreRemoved) {
  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});

  // Now the renderer should have 3 references:
  //   1. Ours in this test.
  //   2. The RouteGraph's.
  //   3. The ThrottleOutput's (because they are linked).
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({context().throttle_output()}));

  under_test_.RemoveRenderer(*renderer);
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());
  EXPECT_THAT(SourceLinks(*output), IsEmpty());
}

TEST_F(RouteGraphTest, CapturersAreUnlinkedWhenHaveNoRoutingProfile) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());
}

TEST_F(RouteGraphTest, CapturersRouteToLastPluggedInput) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                        &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(first_input.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({first_input.get()}));

  auto later_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                        &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(later_input.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({later_input.get()}));
}

TEST_F(RouteGraphTest, CapturersFallbackWhenInputRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                        &context().device_manager(), &context().link_matrix());
  auto later_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                        &context().device_manager(), &context().link_matrix());

  under_test_.AddDevice(first_input.get());
  under_test_.AddDevice(later_input.get());

  under_test_.RemoveDevice(later_input.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({first_input.get()}));

  under_test_.RemoveDevice(first_input.get());
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());
}

TEST_F(RouteGraphTest, RemovingNonLastInputDoesNotRerouteCapturers) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});

  auto first_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                        &context().device_manager(), &context().link_matrix());
  auto second_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                         &context().device_manager(), &context().link_matrix());
  auto last_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                       &context().device_manager(), &context().link_matrix());

  under_test_.AddDevice(first_input.get());
  under_test_.AddDevice(second_input.get());
  under_test_.AddDevice(last_input.get());

  under_test_.RemoveDevice(first_input.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({last_input.get()}));

  under_test_.RemoveDevice(second_input.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({last_input.get()}));
}

TEST_F(RouteGraphTest, CapturersPickUpLastPluggedInputWhenRoutable) {
  auto first_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                        &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(first_input.get());

  auto later_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                        &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(later_input.get());

  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({later_input.get()}));
}

TEST_F(RouteGraphTest, CapturersAreRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});

  auto input = AudioInput::Create("", zx::channel(), &threading_model(),
                                  &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(input.get());
  EXPECT_THAT(DestLinks(*input), UnorderedElementsAreArray({capturer.get()}));
  under_test_.RemoveCapturer(*capturer);
  EXPECT_THAT(DestLinks(*input), IsEmpty());
}

TEST_F(RouteGraphTest, LoopbackCapturersRouteToLastPluggedOutput) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  under_test_.AddDevice(first_output.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({first_output.get()}));

  auto later_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  under_test_.AddDevice(later_output.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({later_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersFallbackWhenOutputRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  auto later_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());

  under_test_.AddDevice(first_output.get());
  under_test_.AddDevice(later_output.get());

  under_test_.RemoveDevice(later_output.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({first_output.get()}));

  under_test_.RemoveDevice(first_output.get());
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());
}

TEST_F(RouteGraphTest, RemovingNonLastOutputDoesNotRerouteLoopbackCapturers) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});

  auto first_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  auto second_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                               &context().link_matrix());
  auto last_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                             &context().link_matrix());

  under_test_.AddDevice(first_output.get());
  under_test_.AddDevice(second_output.get());
  under_test_.AddDevice(last_output.get());

  under_test_.RemoveDevice(second_output.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({last_output.get()}));

  under_test_.RemoveDevice(first_output.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({last_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersPickUpLastPluggedOutputWhenRoutable) {
  auto first_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  under_test_.AddDevice(first_output.get());

  auto later_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                              &context().link_matrix());
  under_test_.AddDevice(later_output.get());

  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({later_output.get()}));
}

TEST_F(RouteGraphTest, LoopbackCapturersAreRemoved) {
  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});

  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());
  EXPECT_THAT(DestLinks(*output), UnorderedElementsAreArray({capturer.get()}));

  under_test_.RemoveCapturer(*capturer);
  EXPECT_THAT(DestLinks(*output), IsEmpty());
}

TEST_F(RouteGraphTest, OutputRouteCategoriesDoNotAffectEachOther) {
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());

  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());

  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());
  EXPECT_THAT(DestLinks(*renderer),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));

  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({output.get()}));
  EXPECT_THAT(DestLinks(*renderer),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));
}

TEST_F(RouteGraphTest, InputRouteCategoriesDoNotAffectOutputs) {
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());

  auto first_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                        &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(first_input.get());

  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({first_input.get()}));

  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  EXPECT_THAT(DestLinks(*renderer),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({first_input.get()}));
}

TEST_F(RouteGraphTest, DoesNotRouteUnroutableRenderer) {
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());

  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  EXPECT_THAT(DestLinks(*renderer), IsEmpty());

  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = false, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});

  auto second_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                               &context().link_matrix());
  under_test_.AddDevice(second_output.get());
  EXPECT_THAT(DestLinks(*renderer), IsEmpty());
}

TEST_F(RouteGraphTest, DoesNotRouteUnroutableCapturer) {
  auto input = AudioInput::Create("", zx::channel(), &threading_model(),
                                  &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(input.get());

  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = false, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});

  auto second_input = AudioInput::Create("", zx::channel(), &threading_model(),
                                         &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(second_input.get());
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());
}

TEST_F(RouteGraphTest, DoesNotRouteUnroutableLoopbackCapturer) {
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());

  auto loopback_capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(loopback_capturer);
  EXPECT_THAT(SourceLinks(*loopback_capturer), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      *loopback_capturer,
      {.routable = false, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});

  auto second_output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                               &context().link_matrix());
  under_test_.AddDevice(second_output.get());
  EXPECT_THAT(SourceLinks(*loopback_capturer), IsEmpty());
}

TEST_F(RouteGraphTest, AcceptsUnroutableRendererWithInvalidFormat) {
  auto renderer = FakeAudioObject::FakeRenderer(/*valid_format=*/false);

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = false, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});

  // Passes by not crashing.
}

TEST_F(RouteGraphTest, UnroutesNewlyUnRoutableRenderer) {
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());

  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  EXPECT_THAT(DestLinks(*renderer), IsEmpty());

  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = false, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  EXPECT_THAT(DestLinks(*renderer), IsEmpty());
}

TEST_F(RouteGraphTest, UnroutesNewlyUnRoutableCapturer) {
  auto input = AudioInput::Create("", zx::channel(), &threading_model(),
                                  &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(input.get());

  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = false, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::SYSTEM_AGENT)});
  EXPECT_THAT(SourceLinks(*capturer), IsEmpty());
}

TEST_F(RouteGraphTest, UnroutesNewlyUnRoutableLoopbackCapturer) {
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());

  auto loopback_capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(loopback_capturer);
  EXPECT_THAT(SourceLinks(*loopback_capturer), IsEmpty());

  under_test_.SetCapturerRoutingProfile(
      *loopback_capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});
  under_test_.SetCapturerRoutingProfile(
      *loopback_capturer,
      {.routable = false, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});
  EXPECT_THAT(SourceLinks(*loopback_capturer), IsEmpty());
}

const audio_stream_unique_id_t kSupportsAllDeviceId = audio_stream_unique_id_t{.data = {0x33}};
const audio_stream_unique_id_t kUnconfiguredDeviceId = audio_stream_unique_id_t{.data = {0x45}};

static const DeviceConfig kConfigWithMediaExternalRoutingPolicy = DeviceConfig(
    /*profiles=*/{{{kSupportsAllDeviceId},
                   DeviceConfig::OutputDeviceProfile(
                       /*eligible_for_loopback=*/true,
                       /*output_usage_support_set=*/
                       StreamUsageSetFromRenderUsages(kFidlRenderUsages))}},
    /*default=*/
    {DeviceConfig::OutputDeviceProfile(
        /*eligible_for_loopback=*/true,
        /*output_usage_support_set=*/{StreamUsage::WithRenderUsage(RenderUsage::MEDIA)})},
    {}, {});

class RouteGraphWithMediaExternalPolicyTest : public RouteGraphTest {
 public:
  RouteGraphWithMediaExternalPolicyTest() : RouteGraphTest(kConfigWithMediaExternalRoutingPolicy) {}
};

TEST_F(RouteGraphWithMediaExternalPolicyTest, MediaRoutesToLastPluggedSupportedDevice) {
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  auto output = output_and_driver.output.get();
  under_test_.AddDevice(output);

  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer, {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output}));

  auto unconfigured_output_and_driver = OutputWithDeviceId(kUnconfiguredDeviceId);
  auto unconfigured_output = unconfigured_output_and_driver.output.get();
  under_test_.AddDevice(unconfigured_output);
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({unconfigured_output}));
}

TEST_F(RouteGraphWithMediaExternalPolicyTest, InterruptionDoesNotRouteToUnsupportedDevice) {
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  auto output = output_and_driver.output.get();
  under_test_.AddDevice(output);

  auto renderer = FakeAudioObject::FakeRenderer();

  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(
      *renderer,
      {.routable = true, .usage = StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION)});
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output}));

  auto unconfigured_output_and_driver = OutputWithDeviceId(kUnconfiguredDeviceId);
  auto unconfigured_output = unconfigured_output_and_driver.output.get();
  under_test_.AddDevice(unconfigured_output);
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output}));
}

const audio_stream_unique_id_t kSupportsLoopbackDeviceId = audio_stream_unique_id_t{.data = {0x7a}};

static const DeviceConfig kConfigWithExternNonLoopbackDevicePolicy = DeviceConfig(
    /*profiles=*/{{{kSupportsAllDeviceId},
                   DeviceConfig::OutputDeviceProfile(
                       /*eligible_for_loopback=*/true,
                       /*output_usage_support_set=*/
                       StreamUsageSetFromRenderUsages(kFidlRenderUsages))},
                  {{kSupportsLoopbackDeviceId},
                   DeviceConfig::OutputDeviceProfile(
                       /*eligible_for_loopback=*/true,
                       /*output_usage_support_set=*/
                       {StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND)})}},
    /*default=*/
    {DeviceConfig::OutputDeviceProfile(
        /*eligible_for_loopback=*/false,
        /*output_usage_support_set=*/
        {StreamUsageSetFromRenderUsages(kFidlRenderUsages)})},
    {}, {});

class RouteGraphWithExternalNonLoopbackDeviceTest : public RouteGraphTest {
 public:
  RouteGraphWithExternalNonLoopbackDeviceTest()
      : RouteGraphTest(kConfigWithExternNonLoopbackDevicePolicy) {}
};

TEST_F(RouteGraphWithExternalNonLoopbackDeviceTest, LoopbackRoutesToLastPluggedSupported) {
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  auto output = output_and_driver.output.get();
  under_test_.AddDevice(output);

  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({output}));

  auto second_output_and_driver = OutputWithDeviceId(kSupportsLoopbackDeviceId);
  auto second_output = second_output_and_driver.output.get();
  under_test_.AddDevice(second_output);
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({second_output}));
}

TEST_F(RouteGraphWithExternalNonLoopbackDeviceTest, LoopbackDoesNotRouteToUnsupportedDevice) {
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  auto output = output_and_driver.output.get();
  under_test_.AddDevice(output);

  auto capturer = FakeAudioObject::FakeCapturer();

  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(
      *capturer,
      {.routable = true, .usage = StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)});
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({output}));

  auto second_output_and_driver = OutputWithDeviceId(kUnconfiguredDeviceId);
  auto second_output = second_output_and_driver.output.get();
  under_test_.AddDevice(second_output);
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({output}));
}

TEST_F(RouteGraphTest, DoesNotUnlinkRendererNotInGraph) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());

  context().link_matrix().LinkObjects(renderer, output, std::make_shared<NoOpLoudnessTransform>());
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output.get()}));

  under_test_.RemoveRenderer(*renderer);
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output.get()}));
}

TEST_F(RouteGraphTest, DoesNotUnlinkCapturerNotInGraph) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto input = AudioInput::Create("", zx::channel(), &threading_model(),
                                  &context().device_manager(), &context().link_matrix());

  context().link_matrix().LinkObjects(input, capturer, std::make_shared<NoOpLoudnessTransform>());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({input.get()}));

  under_test_.RemoveCapturer(*capturer);
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({input.get()}));
}

TEST_F(RouteGraphTest, DoesNotUnlinkLoopbackCapturerNotInGraph) {
  auto loopback_capturer = FakeAudioObject::FakeCapturer();
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());

  context().link_matrix().LinkObjects(output, loopback_capturer,
                                      std::make_shared<NoOpLoudnessTransform>());
  EXPECT_THAT(SourceLinks(*loopback_capturer), UnorderedElementsAreArray({output.get()}));

  under_test_.RemoveCapturer(*loopback_capturer);
  EXPECT_THAT(SourceLinks(*loopback_capturer), UnorderedElementsAreArray({output.get()}));
}

TEST_F(RouteGraphTest, DoesNotRelinkRendererIfRouteDoesNotChange) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());

  const RoutingProfile kRoutableProfile{
      .routable = true,
      .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
  };

  // Add and confirm an initial link has been made.
  under_test_.AddDevice(output.get());
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(*renderer, kRoutableProfile);
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output.get()}));
  EXPECT_EQ(1u, renderer->total_links_formed());

  // Now set the same profile. We should see no change in our routes or link counts.
  under_test_.SetRendererRoutingProfile(*renderer, kRoutableProfile);
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output.get()}));
  EXPECT_EQ(1u, renderer->total_links_formed());
}

TEST_F(RouteGraphTest, DoesNotRelinkCapturerIfRouteDoesNotChange) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto input = AudioInput::Create("", zx::channel(), &threading_model(),
                                  &context().device_manager(), &context().link_matrix());

  const RoutingProfile kRoutableProfile{
      .routable = true,
      .usage = StreamUsage::WithCaptureUsage(CaptureUsage::BACKGROUND),
  };

  // Add and confirm an initial link has been made.
  under_test_.AddDevice(input.get());
  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(*capturer, kRoutableProfile);
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({input.get()}));
  EXPECT_EQ(1u, capturer->total_links_formed());

  // Now set the same profile. We should see no change in our routes or link counts.
  under_test_.SetCapturerRoutingProfile(*capturer, kRoutableProfile);
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({input.get()}));
  EXPECT_EQ(1u, capturer->total_links_formed());
}

TEST_F(RouteGraphTest, DoesNotRelinkCapturerIfUnroutedDeviceIsAdded) {
  auto capturer = FakeAudioObject::FakeCapturer();
  auto input = AudioInput::Create("", zx::channel(), &threading_model(),
                                  &context().device_manager(), &context().link_matrix());
  const RoutingProfile kRoutableProfile{
      .routable = true,
      .usage = StreamUsage::WithCaptureUsage(CaptureUsage::BACKGROUND),
  };

  // Add and confirm an initial link has been made.
  under_test_.AddDevice(input.get());
  under_test_.AddCapturer(capturer);
  under_test_.SetCapturerRoutingProfile(*capturer, kRoutableProfile);
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({input.get()}));
  EXPECT_EQ(1u, capturer->total_links_formed());

  // Now add an unrelated device. We should see no link activity on our capturer.
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());
  under_test_.AddDevice(output.get());
  EXPECT_THAT(SourceLinks(*capturer), UnorderedElementsAreArray({input.get()}));
  EXPECT_EQ(1u, capturer->total_links_formed());
}

TEST_F(RouteGraphTest, DoesNotRelinkRendererIfUnroutedDeviceIsAdded) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto output = FakeAudioOutput::Create(&threading_model(), &context().device_manager(),
                                        &context().link_matrix());

  const RoutingProfile kRoutableProfile{
      .routable = true,
      .usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
  };

  // Add and confirm an initial link has been made.
  under_test_.AddDevice(output.get());
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(*renderer, kRoutableProfile);
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output.get()}));
  EXPECT_EQ(1u, renderer->total_links_formed());

  // Now set the same profile. We should see no change in our routes or link counts.
  auto input = AudioInput::Create("", zx::channel(), &threading_model(),
                                  &context().device_manager(), &context().link_matrix());
  under_test_.AddDevice(input.get());
  EXPECT_THAT(DestLinks(*renderer), UnorderedElementsAreArray({output.get()}));
  EXPECT_EQ(1u, renderer->total_links_formed());
}

static const DeviceConfig kConfigWithUltrasound = DeviceConfig(
    /*profiles=*/{{{kSupportsAllDeviceId},
                   DeviceConfig::OutputDeviceProfile(
                       /*eligible_for_loopback=*/true,
                       /*output_usage_support_set=*/
                       StreamUsageSetFromRenderUsages(kRenderUsages))}},
    /*default=*/
    {DeviceConfig::OutputDeviceProfile(
        /*eligible_for_loopback=*/true,
        /*output_usage_support_set=*/{StreamUsage::WithRenderUsage(RenderUsage::MEDIA)})},
    {}, {});

class RouteGraphUltrasoundTest : public RouteGraphTest {
 public:
  RouteGraphUltrasoundTest() : RouteGraphTest(kConfigWithUltrasound) {}
};

TEST_F(RouteGraphUltrasoundTest, DoNotLinkUltrasoundRendererToDeviceWithLowRate) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  output_and_driver.output->set_format(
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = 1,
                         .frames_per_second = 48000,
                     })
          .take_value());
  RunLoopUntilIdle();
  auto output = output_and_driver.output;
  const RoutingProfile kRoutableProfile{
      .routable = true,
      .usage = StreamUsage::WithRenderUsage(RenderUsage::ULTRASOUND),
  };

  under_test_.AddDevice(output.get());
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(*renderer, kRoutableProfile);

  EXPECT_THAT(DestLinks(*renderer),
              UnorderedElementsAreArray(std::vector<AudioObject*>{context().throttle_output()}));
}

TEST_F(RouteGraphUltrasoundTest, LinkUltrasoundRendererToDeviceWithSufficientRate) {
  auto renderer = FakeAudioObject::FakeRenderer();
  auto output_and_driver = OutputWithDeviceId(kSupportsAllDeviceId);
  output_and_driver.output->set_format(
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = 1,
                         .frames_per_second = 96000,
                     })
          .take_value());
  RunLoopUntilIdle();

  auto output = output_and_driver.output;
  const RoutingProfile kRoutableProfile{
      .routable = true,
      .usage = StreamUsage::WithRenderUsage(RenderUsage::ULTRASOUND),
  };

  under_test_.AddDevice(output.get());
  under_test_.AddRenderer(renderer);
  under_test_.SetRendererRoutingProfile(*renderer, kRoutableProfile);

  EXPECT_THAT(DestLinks(*renderer),
              UnorderedElementsAreArray(std::vector<AudioObject*>{output.get()}));
}

}  // namespace
}  // namespace media::audio
