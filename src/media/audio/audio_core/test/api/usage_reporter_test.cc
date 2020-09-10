// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

using AudioCaptureUsage = fuchsia::media::AudioCaptureUsage;
using AudioRenderUsage = fuchsia::media::AudioRenderUsage;
using AudioSampleFormat = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

namespace {
class FakeUsageWatcher : public fuchsia::media::UsageWatcher {
 public:
  explicit FakeUsageWatcher(TestFixture* fixture) : binding_(this) {
    fixture->AddErrorHandler(binding_, "FakeUsageWatcher");
  }

  fidl::InterfaceHandle<fuchsia::media::UsageWatcher> NewBinding() { return binding_.NewBinding(); }

  using Handler =
      std::function<void(fuchsia::media::Usage usage, fuchsia::media::UsageState usage_state)>;

  void SetNextHandler(Handler h) { next_handler_ = h; }

 private:
  void OnStateChanged(fuchsia::media::Usage usage, fuchsia::media::UsageState usage_state,
                      OnStateChangedCallback callback) override {
    if (next_handler_) {
      next_handler_(std::move(usage), std::move(usage_state));
      next_handler_ = nullptr;
    }
    callback();
  }

  fidl::Binding<fuchsia::media::UsageWatcher> binding_;
  Handler next_handler_;
};
}  // namespace

class UsageReporterTest : public HermeticAudioTest {
 protected:
  void SetUp() {
    HermeticAudioTest::SetUp();
    audio_core_->ResetInteractions();
  }

  struct Controller {
    Controller(TestFixture* fixture) : fake_watcher(fixture) {}

    fuchsia::media::UsageReporterPtr usage_reporter;
    FakeUsageWatcher fake_watcher;
  };

  std::unique_ptr<Controller> CreateController(AudioRenderUsage u) {
    fuchsia::media::Usage usage;
    usage.set_render_usage(u);

    auto c = std::make_unique<Controller>(this);
    environment()->ConnectToService(c->usage_reporter.NewRequest());
    AddErrorHandler(c->usage_reporter, "UsageReporter");
    c->usage_reporter->Watch(std::move(usage), c->fake_watcher.NewBinding());

    return c;
  }

  std::unique_ptr<Controller> CreateController(AudioCaptureUsage u) {
    fuchsia::media::Usage usage;
    usage.set_capture_usage(u);

    auto c = std::make_unique<Controller>(this);
    environment()->ConnectToService(c->usage_reporter.NewRequest());
    AddErrorHandler(c->usage_reporter, "UsageReporter");
    c->usage_reporter->Watch(std::move(usage), c->fake_watcher.NewBinding());

    return c;
  }

  void StartRendererWithUsage(AudioRenderUsage usage) {
    auto format = Format::Create<AudioSampleFormat::SIGNED_16>(1, 8000).value();  // arbitrary
    auto r = CreateAudioRenderer(format, 1024, usage);
    r->fidl()->PlayNoReply(0, 0);
  }

  void StartCapturerWithUsage(AudioCaptureUsage usage) {
    auto format = Format::Create<AudioSampleFormat::SIGNED_16>(1, 8000).value();  // arbitrary
    fuchsia::media::InputAudioCapturerConfiguration cfg;
    cfg.set_usage(usage);
    auto c = CreateAudioCapturer(
        format, 1024, fuchsia::media::AudioCapturerConfiguration::WithInput(std::move(cfg)));
    c->fidl()->StartAsyncCapture(1024);
  }
};

TEST_F(UsageReporterTest, RenderUsageInitialState) {
  auto c = CreateController(AudioRenderUsage::MEDIA);

  fuchsia::media::Usage last_usage;
  fuchsia::media::UsageState last_state;
  c->fake_watcher.SetNextHandler(AddCallback(
      "OnStateChange",
      [&last_usage, &last_state](fuchsia::media::Usage usage, fuchsia::media::UsageState state) {
        last_usage = std::move(usage);
        last_state = std::move(state);
      }));

  // The initial callback happens immediately.
  ExpectCallback();
  EXPECT_TRUE(last_state.is_unadjusted());
  EXPECT_TRUE(last_usage.is_render_usage());
  EXPECT_EQ(last_usage.render_usage(), AudioRenderUsage::MEDIA);
}

TEST_F(UsageReporterTest, RenderUsageDucked) {
  auto c = CreateController(AudioRenderUsage::MEDIA);

  // The initial callback happens immediately.
  c->fake_watcher.SetNextHandler(AddCallback("OnStateChange InitialCall"));
  ExpectCallback();

  fuchsia::media::Usage last_usage;
  fuchsia::media::UsageState last_state;
  c->fake_watcher.SetNextHandler(AddCallback(
      "OnStateChange",
      [&last_usage, &last_state](fuchsia::media::Usage usage, fuchsia::media::UsageState state) {
        last_usage = std::move(usage);
        last_state = std::move(state);
      }));

  // Duck MEDIA when SYSTEM_AGENT is active.
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(AudioRenderUsage::SYSTEM_AGENT);
    affected.set_render_usage(AudioRenderUsage::MEDIA);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::DUCK);
  }

  StartRendererWithUsage(AudioRenderUsage::SYSTEM_AGENT);
  ExpectCallback();
  EXPECT_TRUE(last_state.is_ducked());
  EXPECT_TRUE(last_usage.is_render_usage());
  EXPECT_EQ(last_usage.render_usage(), AudioRenderUsage::MEDIA);
}

TEST_F(UsageReporterTest, RenderUsageMuted) {
  auto c = CreateController(AudioRenderUsage::MEDIA);

  // The initial callback happens immediately.
  c->fake_watcher.SetNextHandler(AddCallback("OnStateChange InitialCall"));
  ExpectCallback();

  fuchsia::media::Usage last_usage;
  fuchsia::media::UsageState last_state;
  c->fake_watcher.SetNextHandler(AddCallback(
      "OnStateChange",
      [&last_usage, &last_state](fuchsia::media::Usage usage, fuchsia::media::UsageState state) {
        last_usage = std::move(usage);
        last_state = std::move(state);
      }));

  // Mute MEDIA when SYSTEM_AGENT is active.
  {
    fuchsia::media::Usage active, affected;
    active.set_render_usage(AudioRenderUsage::SYSTEM_AGENT);
    affected.set_render_usage(AudioRenderUsage::MEDIA);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::MUTE);
  }

  StartRendererWithUsage(AudioRenderUsage::SYSTEM_AGENT);
  ExpectCallback();
  EXPECT_TRUE(last_state.is_muted());
  EXPECT_TRUE(last_usage.is_render_usage());
  EXPECT_EQ(last_usage.render_usage(), AudioRenderUsage::MEDIA);
}

TEST_F(UsageReporterTest, CaptureUsageInitialState) {
  auto c = CreateController(AudioCaptureUsage::COMMUNICATION);

  fuchsia::media::Usage last_usage;
  fuchsia::media::UsageState last_state;
  c->fake_watcher.SetNextHandler(AddCallback(
      "OnStateChange",
      [&last_usage, &last_state](fuchsia::media::Usage usage, fuchsia::media::UsageState state) {
        last_usage = std::move(usage);
        last_state = std::move(state);
      }));

  // The initial callback happens immediately.
  ExpectCallback();
  EXPECT_TRUE(last_state.is_unadjusted());
  EXPECT_TRUE(last_usage.is_capture_usage());
  EXPECT_EQ(last_usage.capture_usage(), AudioCaptureUsage::COMMUNICATION);
}

TEST_F(UsageReporterTest, CaptureUsageDucked) {
  auto c = CreateController(AudioCaptureUsage::COMMUNICATION);

  // The initial callback happens immediately.
  c->fake_watcher.SetNextHandler(AddCallback("OnStateChange InitialCall"));
  ExpectCallback();

  fuchsia::media::Usage last_usage;
  fuchsia::media::UsageState last_state;
  c->fake_watcher.SetNextHandler(AddCallback(
      "OnStateChange",
      [&last_usage, &last_state](fuchsia::media::Usage usage, fuchsia::media::UsageState state) {
        last_usage = std::move(usage);
        last_state = std::move(state);
      }));

  // Duck COMMUNICATION when SYSTEM_AGENT is active.
  {
    fuchsia::media::Usage active, affected;
    active.set_capture_usage(AudioCaptureUsage::SYSTEM_AGENT);
    affected.set_capture_usage(AudioCaptureUsage::COMMUNICATION);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::DUCK);
  }

  StartCapturerWithUsage(AudioCaptureUsage::SYSTEM_AGENT);
  ExpectCallback();
  EXPECT_TRUE(last_state.is_ducked());
  EXPECT_TRUE(last_usage.is_capture_usage());
  EXPECT_EQ(last_usage.capture_usage(), AudioCaptureUsage::COMMUNICATION);
}

TEST_F(UsageReporterTest, CaptureUsageMuted) {
  auto c = CreateController(AudioCaptureUsage::COMMUNICATION);

  // The initial callback happens immediately.
  c->fake_watcher.SetNextHandler(AddCallback("OnStateChange InitialCall"));
  ExpectCallback();

  fuchsia::media::Usage last_usage;
  fuchsia::media::UsageState last_state;
  c->fake_watcher.SetNextHandler(AddCallback(
      "OnStateChange",
      [&last_usage, &last_state](fuchsia::media::Usage usage, fuchsia::media::UsageState state) {
        last_usage = std::move(usage);
        last_state = std::move(state);
      }));

  // Mute COMMUNICATION when SYSTEM_AGENT is active.
  {
    fuchsia::media::Usage active, affected;
    active.set_capture_usage(AudioCaptureUsage::SYSTEM_AGENT);
    affected.set_capture_usage(AudioCaptureUsage::COMMUNICATION);
    audio_core_->SetInteraction(std::move(active), std::move(affected),
                                fuchsia::media::Behavior::MUTE);
  }

  StartCapturerWithUsage(AudioCaptureUsage::SYSTEM_AGENT);
  ExpectCallback();
  EXPECT_TRUE(last_state.is_muted());
  EXPECT_TRUE(last_usage.is_capture_usage());
  EXPECT_EQ(last_usage.capture_usage(), AudioCaptureUsage::COMMUNICATION);
}

}  // namespace media::audio::test
