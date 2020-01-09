// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

//
// AudioTest
//
class AudioTest : public HermeticAudioCoreTest {
 protected:
  void TearDown() override;

  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;
};

//
// UsageVolumeControlTest
//
class UsageVolumeControlTest : public HermeticAudioCoreTest {};

//
// AudioTest implementation
//
void AudioTest::TearDown() {
  audio_renderer_.Unbind();
  audio_capturer_.Unbind();

  HermeticAudioCoreTest::TearDown();
}

//
// UsageReporterTest
//
class UsageReporterTest : public HermeticAudioCoreTest {
 protected:
  class FakeUsageWatcher : public fuchsia::media::UsageWatcher {
   public:
    explicit FakeUsageWatcher(fit::closure completer)
        : completer_(std::move(completer)), binding_(this) {}

    fidl::InterfaceHandle<fuchsia::media::UsageWatcher> Bind() { return binding_.NewBinding(); }

   private:
    void OnStateChanged(fuchsia::media::Usage _usage, fuchsia::media::UsageState _usage_state,
                        OnStateChangedCallback callback) override {
      callback();
      completer_();
    }

    fit::closure completer_;
    fidl::Binding<fuchsia::media::UsageWatcher> binding_;
  };
};

//
// Test that the user is connected to the usage reporter.
//
TEST_F(UsageReporterTest, ConnectToUsageReporter) {
  fit::closure completer = CompletionCallback([] {});

  fuchsia::media::UsageReporterPtr audio_core;
  environment()->ConnectToService(audio_core.NewRequest());
  audio_core.set_error_handler(ErrorHandler());

  fuchsia::media::Usage usage;
  usage.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);

  FakeUsageWatcher watcher(std::move(completer));
  audio_core->Watch(std::move(usage), watcher.Bind());

  ExpectCallback();
}

//
// Audio validation
// Tests of the asynchronous Audio interface.
//
// In some tests below, we run the message loop, so that any channel-disconnect
// from error -- with subsequent reset of the interface ptr -- can take effect.
//
// Test creation and interface independence of AudioRenderer.
// The following 4 conditions are validated:
// 1. Audio can create AudioRenderer.
// 2. Audio persists after created AudioRenderer is destroyed.
// 3. AudioRenderer2 persists after Audio2 is destroyed.
// 4. Asynchronous Audio can create synchronous AudioRenderer, too.
TEST_F(AudioTest, CreateAudioRenderer) {
  audio_core_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_.set_error_handler(ErrorHandler());

  fuchsia::media::AudioRendererSyncPtr audio_renderer_sync;
  audio_core_->CreateAudioRenderer(audio_renderer_sync.NewRequest());

  fuchsia::media::AudioCorePtr audio_core_2;
  environment()->ConnectToService(audio_core_2.NewRequest());
  audio_core_2.set_error_handler(ErrorHandler());

  fuchsia::media::AudioRendererPtr audio_renderer_2;
  audio_core_2->CreateAudioRenderer(audio_renderer_2.NewRequest());
  audio_renderer_2.set_error_handler(ErrorHandler());

  // Before unbinding these, verify they survived this far.
  EXPECT_TRUE(audio_core_2.is_bound());
  audio_core_2.Unbind();

  EXPECT_TRUE(audio_renderer_.is_bound());
  audio_renderer_.Unbind();

  // ...allow them to completely unbind. Will it affect their parent/child?
  audio_renderer_2->GetMinLeadTime(CompletionCallback([](int64_t) {}));
  ExpectCallback();

  // Validate AudioRendererSync was successfully created.
  EXPECT_TRUE(audio_renderer_sync.is_bound());

  // Validate child AudioRenderer2 persists after parent Audio2 was unbound.
  EXPECT_TRUE(audio_renderer_2.is_bound());

  // TearDown will validate that parent Audio survived after child unbound.
}

// Test creation and interface independence of AudioCapturer.
// The following 4 conditions are validated:
// 1. Audio can create AudioCapturer.
// 2. Audio persists after created AudioCapturer is destroyed.
// 3. AudioCapturer2 persists after Audio2 is destroyed.
// 4. Asynchronous Audio can create synchronous AudioCapturer, too.
TEST_F(AudioTest, CreateAudioCapturer) {
  audio_core_->CreateAudioCapturer(false, audio_capturer_.NewRequest());
  audio_capturer_.set_error_handler(ErrorHandler());

  fuchsia::media::AudioCapturerSyncPtr audio_capturer_sync;
  audio_core_->CreateAudioCapturer(false, audio_capturer_sync.NewRequest());

  fuchsia::media::AudioCorePtr audio_core_2;
  environment()->ConnectToService(audio_core_2.NewRequest());
  audio_core_2.set_error_handler(ErrorHandler());

  fuchsia::media::AudioCapturerPtr audio_capturer_2;
  audio_core_2->CreateAudioCapturer(false, audio_capturer_2.NewRequest());
  audio_capturer_2.set_error_handler(ErrorHandler());

  // Before unbinding these, verify they survived this far.
  EXPECT_TRUE(audio_core_2.is_bound());
  audio_core_2.Unbind();

  EXPECT_TRUE(audio_capturer_.is_bound());
  audio_capturer_.Unbind();

  // ...allow them to completely unbind. Will it affect their parent/child?
  audio_capturer_2->GetStreamType(CompletionCallback([](fuchsia::media::StreamType) {}));
  ExpectCallback();

  // Validate AudioCapturerSync was successfully created.
  EXPECT_TRUE(audio_capturer_sync.is_bound());

  // Validate AudioCapturer2 persists after Audio2 was unbound.
  EXPECT_TRUE(audio_capturer_2.is_bound());

  // TearDown will validate that parent Audio survived after child unbound.
}

TEST_F(UsageVolumeControlTest, ConnectToUsageVolume) {
  fuchsia::media::AudioCorePtr audio_core;
  environment()->ConnectToService(audio_core.NewRequest());
  audio_core.set_error_handler(ErrorHandler());

  fuchsia::media::audio::VolumeControlPtr client1;
  fuchsia::media::audio::VolumeControlPtr client2;

  fuchsia::media::Usage usage;
  usage.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);

  audio_core->BindUsageVolumeControl(fidl::Clone(usage), client1.NewRequest());
  audio_core->BindUsageVolumeControl(fidl::Clone(usage), client2.NewRequest());

  float volume = 0.0;
  bool muted = false;
  client2.events().OnVolumeMuteChanged =
      CompletionCallback([&volume, &muted](float new_volume, bool new_muted) {
        volume = new_volume;
        muted = new_muted;
      });

  ExpectCallback();
  EXPECT_FLOAT_EQ(volume, 1.0);

  client1->SetVolume(0.5);
  ExpectCallback();
  EXPECT_FLOAT_EQ(volume, 0.5);
  EXPECT_EQ(muted, false);

  client1->SetMute(true);
  ExpectCallback();
  EXPECT_EQ(muted, true);
}

//
// TODO(mpuryear): "fuzz" tests (FIDL-compliant but protocol-inconsistent).
//

}  // namespace media::audio::test
