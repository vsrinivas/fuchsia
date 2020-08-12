// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>

#include <cmath>
#include <memory>

#include "src/media/audio/lib/test/constants.h"
#include "src/media/audio/lib/test/hermetic_audio_test.h"
#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {

namespace {
class FakeGainListener : public fuchsia::media::UsageGainListener {
 public:
  explicit FakeGainListener(TestFixture* fixture) : binding_(this) {
    fixture->AddErrorHandler(binding_, "FakeGainListener");
  }

  fidl::InterfaceHandle<fuchsia::media::UsageGainListener> NewBinding() {
    return binding_.NewBinding();
  }

  using Handler = std::function<void(bool muted, float gain_db)>;

  void SetNextHandler(Handler h) { next_handler_ = h; }

 private:
  // |fuchsia::media::UsageGainListener|
  void OnGainMuteChanged(bool muted, float gain_db, OnGainMuteChangedCallback callback) final {
    if (next_handler_) {
      next_handler_(muted, gain_db);
      next_handler_ = nullptr;
    }
    callback();
  }

  fidl::Binding<fuchsia::media::UsageGainListener> binding_;
  Handler next_handler_;
};
}  // namespace

class UsageGainReporterTest : public HermeticAudioTest {
 public:
  void SetUp() {
    HermeticAudioTest::SetUp();

    // We need to create an output device to listen on.
    // The specific choice of format doesn't matter here, any format will do.
    constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
    constexpr auto kSampleRate = 48000;
    auto format = Format::Create<kSampleFormat>(2, kSampleRate).value();
    CreateOutput(device_id_array_, format, kSampleRate /* 1s buffer */);
  }

  struct Controller {
    Controller(TestFixture* fixture) : fake_listener(fixture) {}

    fuchsia::media::audio::VolumeControlPtr volume_control;
    fuchsia::media::UsageGainReporterPtr gain_reporter;
    FakeGainListener fake_listener;
  };

  std::unique_ptr<Controller> CreateController(fuchsia::media::AudioRenderUsage u) {
    fuchsia::media::Usage usage = fuchsia::media::Usage::WithRenderUsage(std::move(u));

    auto c = std::make_unique<Controller>(this);
    audio_core_->BindUsageVolumeControl(fidl::Clone(usage), c->volume_control.NewRequest());
    AddErrorHandler(c->volume_control, "VolumeControl");

    environment()->ConnectToService(c->gain_reporter.NewRequest());
    AddErrorHandler(c->gain_reporter, "GainReporter");
    c->gain_reporter->RegisterListener(device_id_string_, fidl::Clone(usage),
                                       c->fake_listener.NewBinding());

    return c;
  }

  // The device ID is arbitrary.
  const std::string device_id_string_ = "ff000000000000000000000000000000";
  const audio_stream_unique_id_t device_id_array_ = {{
      0xff,
      0x00,
  }};
};

TEST_F(UsageGainReporterTest, SetVolumeAndMute) {
  auto c = CreateController(fuchsia::media::AudioRenderUsage::MEDIA);

  // The initial callback happens immediately.
  c->fake_listener.SetNextHandler(AddCallback("OnGainMuteChanged InitialCall"));
  ExpectCallback();

  bool last_muted;
  float last_gain_db;

  auto set_callback = [this, &c, &last_muted, &last_gain_db](std::string stage) {
    last_muted = true;
    last_gain_db = kTooHighGainDb;
    c->fake_listener.SetNextHandler(
        AddCallback("OnGainMuteChanged after " + stage,
                    [&last_muted, &last_gain_db](bool muted, float gain_db) {
                      last_muted = muted;
                      last_gain_db = gain_db;
                    }));
  };

  set_callback("SetVolume(0)");
  c->volume_control->SetVolume(0);
  ExpectCallback();
  EXPECT_FALSE(last_muted);
  EXPECT_FLOAT_EQ(last_gain_db, fuchsia::media::audio::MUTED_GAIN_DB);

  set_callback("SetVolume(1)");
  c->volume_control->SetVolume(1);
  ExpectCallback();
  EXPECT_FALSE(last_muted);
  EXPECT_FLOAT_EQ(last_gain_db, 0);

  // TODO(fxbug.dev/54949): SetMute(true) events are broken
#if 0
  set_callback("SetMute(true)");
  c->volume_control->SetMute(true);
  ExpectCallback();
  EXPECT_TRUE(last_muted);
  EXPECT_FLOAT_EQ(last_gain_db, fuchsia::media::audio::MUTED_GAIN_DB);

  // Unmute should restore the volume.
  set_callback("SetMute(false)");
  c->volume_control->SetMute(false);
  ExpectCallback();
  EXPECT_FALSE(last_muted);
  EXPECT_FLOAT_EQ(last_gain_db, 0);
#endif
}

TEST_F(UsageGainReporterTest, RoutedCorrectly) {
  auto c1 = CreateController(fuchsia::media::AudioRenderUsage::MEDIA);
  auto c2 = CreateController(fuchsia::media::AudioRenderUsage::BACKGROUND);

  // The initial callbacks happen immediately.
  c1->fake_listener.SetNextHandler(AddCallback("OnGainMuteChanged1 InitialCall"));
  c2->fake_listener.SetNextHandler(AddCallback("OnGainMuteChanged2 InitialCall"));
  ExpectCallback();

  // Routing to c1.
  c1->fake_listener.SetNextHandler(AddCallback("OnGainMuteChanged1 RouteTo1"));
  c2->fake_listener.SetNextHandler(AddUnexpectedCallback("OnGainMuteChanged2 RouteTo1"));
  c1->volume_control->SetVolume(0);
  ExpectCallback();

  // Routing to c2.
  c1->fake_listener.SetNextHandler(AddUnexpectedCallback("OnGainMuteChanged1 RouteTo2"));
  c2->fake_listener.SetNextHandler(AddCallback("OnGainMuteChanged2 RouteTo2"));
  c2->volume_control->SetVolume(0);
  ExpectCallback();
}

}  // namespace media::audio::test
