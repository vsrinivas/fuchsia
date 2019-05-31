// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

#include "gtest/gtest.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/media/audio/audio_core/test/audio_device_test.h"
#include "src/media/audio/audio_core/test/virtual_audio_device_test.h"

namespace media::audio::test {

class AudioDeviceEnvironment : public testing::Environment {
 public:
  // Do any binary-wide or cross-test-suite setup, before any test suite runs.
  // Note: if --gtest_repeat is used, this is called at start of EVERY repeat.
  //
  // On assert-false during this SetUp method, no test cases run, and they may
  // display as passed. However, the overall binary returns non-zero (fail).
  //
  // Before any test cases in this program, synchronously connect to the service
  // to ensure that audio components are present and loaded.
  void SetUp() override {
    testing::Environment::SetUp();

    async::Loop loop(&kAsyncLoopConfigAttachToThread);

    // This is an unchanging input for the entire component; get it once here.
    auto startup_context = sys::ComponentContext::Create();

    // We use this Control to enable virtualaudio immediately prior to test
    // suites that require it, and to disable it immediately afterward.
    fuchsia::virtualaudio::ControlSyncPtr control;
    startup_context->svc()->Connect(control.NewRequest());

    // As test binary starts, disable any lingering virtual audio devices.
    // Because this is a synchronous call, by the time it returns, DdkRemove has
    // been called on each virtual audio device.
    AudioDeviceTest::SetControl(std::move(control));
    AudioDeviceTest::DisableVirtualDevices();

    // Unlike environment_services, each test case creates fresh FIDL instances.
    // In this one-time setup code we use a temp local var instance: it merely
    // "demand-pages" other components and is not subsequently referenced.
    //
    // Note that we are using the Synchronous version of this interface....
    fuchsia::media::AudioCoreSyncPtr audio_core_sync;
    startup_context->svc()->Connect(audio_core_sync.NewRequest());
    audio_core_sync->EnableDeviceSettings(false);

    fuchsia::media::AudioDeviceEnumeratorSyncPtr audio_dev_enum_sync;
    startup_context->svc()->Connect(audio_dev_enum_sync.NewRequest());

    // This FIDL method has a callback; calling it SYNCHRONOUSLY guarantees
    // that services are loaded and running before the method itself returns.
    //
    // This is not the case for sync calls without callback (nor async calls),
    // because of the pipelining inherent in FIDL's design.
    uint64_t default_input;
    bool connected_to_audio_device_enumerator_service =
        (audio_dev_enum_sync->GetDefaultInputDevice(&default_input) == ZX_OK);

    // On assert-false, no test cases run, and they may display as passed.
    // However, the overall binary returns non-zero (fail).
    ASSERT_TRUE(connected_to_audio_device_enumerator_service);

    // Save these for all to use
    AudioDeviceTest::SetStartupContext(std::move(startup_context));
  }
};

}  // namespace media::audio::test

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  // gtest takes ownership of registered environments: **do not delete them**!
  testing::AddGlobalTestEnvironment(
      new media::audio::test::AudioDeviceEnvironment);

  // TODO(mpuryear): create and use a '--stress' switch here, to execute a set
  // of longhaul resource-exhaustion-focused tests on these interfaces.

  int result = RUN_ALL_TESTS();

  return result;
}
