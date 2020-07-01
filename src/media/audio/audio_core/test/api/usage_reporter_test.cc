// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/tuning/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

class UsageReporterTest : public HermeticAudioTest {
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

// TODO(50645): More complete testing of the integration with renderers
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

}  // namespace media::audio::test
