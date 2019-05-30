// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "gtest/gtest.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/lib/test/audio_core_test_base.h"

namespace media::audio::test {

class AudioFidlEnvironment : public testing::Environment {
 public:
  // Do any binary-wide or cross-test-suite setup, before any test suite runs.
  // Note: if --gtest_repeat is used, this is called at start of EVERY repeat.
  //
  // On assert-false during this SetUp method, no test cases run, and they may
  // display as passed. However, the overall binary returns non-zero (fail).
  //
  // Before any test cases in this test program, synchronously connect to
  // audio_core, to ensure that components are present and loaded.
  void SetUp() override {
    testing::Environment::SetUp();

    async::Loop loop(&kAsyncLoopConfigAttachToThread);

    auto startup_context = sys::ComponentContext::Create();

    // Each test case creates fresh FIDL instances. This one-time setup code
    // uses a temp local var instance to "demand-page" other components and does
    // not subsequently reference it.
    startup_context->svc()->Connect(audio_core_sync_.NewRequest());
    audio_core_sync_->EnableDeviceSettings(false);

    // Note that we are using Synchronous versions of these interfaces....
    fuchsia::media::AudioRendererSyncPtr audio_renderer_sync;
    audio_core_sync_->CreateAudioRenderer(audio_renderer_sync.NewRequest());

    // This FIDL method has a callback; calling it SYNCHRONOUSLY guarantees
    // that services are loaded and running before the method itself returns.
    //
    // This is not the case for sync calls WITHOUT callback (nor async calls),
    // because of the pipelining inherent in FIDL's design.
    zx_duration_t lead_time;
    bool connected_to_audio_service =
        (audio_renderer_sync->GetMinLeadTime(&lead_time) == ZX_OK);

    // On assert-false, no test cases run, and they may display as passed.
    // However, the overall binary returns non-zero (fail).
    ASSERT_TRUE(connected_to_audio_service);

    AudioTestBase::SetStartupContext(std::move(startup_context));
  }

 private:
  fuchsia::media::AudioCoreSyncPtr audio_core_sync_;
};

}  // namespace media::audio::test

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  // gtest takes ownership of registered environments: **do not delete them**!
  testing::AddGlobalTestEnvironment(
      new media::audio::test::AudioFidlEnvironment);

  // TODO(mpuryear): create and use a '--stress' switch here, to execute a set
  // of longhaul resource-exhaustion-focused tests on these interfaces.

  int result = RUN_ALL_TESTS();

  return result;
}
