// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/component/cpp/environment_services_helper.h"
#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/test/audio_tests_shared.h"

namespace media::audio::test {

class AudioFidlEnvironment : public ::testing::Environment {
 public:
  // Before any test cases in this test program, synchronously connect to Audio,
  // to ensure that the audio and audio_core components are present and loaded.
  void SetUp() override {
    auto environment_services = component::GetEnvironmentServices();

    // Each test case creates fresh FIDL instances. This one-time setup code
    // uses a temp local var instance to "demand-page" other components and does
    // not subsequently reference it.
    fuchsia::media::AudioSyncPtr audio;
    environment_services->ConnectToService(audio.NewRequest());

    // Note that we are using Synchronous versions of these interfaces....
    fuchsia::media::AudioRendererSyncPtr audio_renderer;
    audio->CreateAudioRenderer(audio_renderer.NewRequest());

    // This FIDL method has a callback; calling it SYNCHRONOUSLY guarantees
    // that services are loaded and running before the method itself returns.
    //
    // This is not the case for sync calls WITHOUT callback (nor async calls),
    // because of the pipelining inherent in FIDL's design.
    zx_duration_t lead_time;
    bool connected_to_audio_service =
        (audio_renderer->GetMinLeadTime(&lead_time) == ZX_OK);

    // On assert-false, no test cases run, and they may display as passed.
    // However, the overall binary returns non-zero (fail).
    ASSERT_TRUE(connected_to_audio_service);
  }

  ///// If needed, these (overriding) functions would also need to be public.
  // void TearDown() override {}
  // ~AudioFidlEnvironment() override {}
};

}  // namespace media::audio::test

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  // gtest takes ownership of registered environments: **do not delete them**!
  ::testing::AddGlobalTestEnvironment(
      new ::media::audio::test::AudioFidlEnvironment);

  // TODO(mpuryear): create and use a '--stress' switch here, to execute a set
  // of longhaul resource-exhaustion-focused tests on these interfaces.

  int result = RUN_ALL_TESTS();

  return result;
}
