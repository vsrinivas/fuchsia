// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_TEST_BASE_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_TEST_BASE_H_

#include <lib/sys/cpp/component_context.h>

#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {

//
// AudioTestBase
//
// This set of tests verifies asynchronous usage of audio_core protocols.
class AudioTestBase : public TestFixture {
 public:
  static void SetStartupContext(
      std::unique_ptr<sys::ComponentContext> startup_context) {
    startup_context_ = std::move(startup_context);
  }

 protected:
  static void SetUpTestSuite();

  static std::unique_ptr<sys::ComponentContext> startup_context_;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_TEST_BASE_H_
