// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/tuning/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

class UsageGainReporterTest : public HermeticAudioTest {
 protected:
  static void SetUpTestSuite() {
    HermeticAudioTest::SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options{
        .audio_core_config_data_path = "/pkg/data/test_output",
    });
  }

  class FakeGainListener : public fuchsia::media::UsageGainListener {
   public:
    explicit FakeGainListener(fit::closure completer)
        : completer_(std::move(completer)), binding_(this) {
      binding_.set_error_handler([](zx_status_t status) { ASSERT_EQ(status, ZX_OK); });
    }

    fidl::InterfaceHandle<fuchsia::media::UsageGainListener> NewBinding() {
      return binding_.NewBinding();
    }

    bool muted() const { return last_muted_; }

    float gain_db() const { return last_gain_db_; }

   private:
    // |fuchsia::media::UsageGainListener|
    void OnGainMuteChanged(bool muted, float gain_db, OnGainMuteChangedCallback callback) final {
      last_muted_ = muted;
      last_gain_db_ = gain_db;
      completer_();
    }

    fit::closure completer_;
    fidl::Binding<fuchsia::media::UsageGainListener> binding_;
    bool last_muted_ = false;
    float last_gain_db_ = 0.0;
  };

  // This matches the configuration in test_output_audio_core_config.json
  const std::string device_id_string_ = "ffffffffffffffffffffffffffffffff";
  const audio_stream_unique_id_t device_id_array_ = {{
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
  }};
};

// Test that the user is connected to the usage gain reporter.
// TODO(50645): Also test muted
TEST_F(UsageGainReporterTest, ConnectToUsageGainReporter) {
  fit::closure completer = CompletionCallback([] {});

  // The specific choice of format doesn't matter here, any output device will do.
  constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
  constexpr auto kSampleRate = 48000;
  auto format = Format::Create<kSampleFormat>(2, kSampleRate).value();
  CreateOutput(device_id_array_, format, kSampleRate /* 1s buffer */);

  fuchsia::media::Usage usage;
  usage.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);

  fuchsia::media::audio::VolumeControlPtr volume_control;
  audio_core_->BindUsageVolumeControl(fidl::Clone(usage), volume_control.NewRequest());

  fuchsia::media::UsageGainReporterPtr gain_reporter;
  environment()->ConnectToService(gain_reporter.NewRequest());
  gain_reporter.set_error_handler(ErrorHandler());

  auto fake_listener = std::make_unique<FakeGainListener>(std::move(completer));
  gain_reporter->RegisterListener(device_id_string_, fidl::Clone(usage),
                                  fake_listener->NewBinding());

  volume_control->SetVolume(1.0);
  ExpectCallback();
  EXPECT_FALSE(fake_listener->muted());
  EXPECT_FLOAT_EQ(fake_listener->gain_db(), 0.0);
}

}  // namespace media::audio::test
