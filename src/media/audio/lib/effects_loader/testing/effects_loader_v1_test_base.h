// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_EFFECTS_LOADER_V1_TEST_BASE_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_EFFECTS_LOADER_V1_TEST_BASE_H_

#include <gtest/gtest.h>

#include "src/media/audio/effects/test_effects/test_effects_v1.h"
#include "src/media/audio/lib/effects_loader/effects_loader_v1.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects_v1.h"

namespace media::audio::testing {

// The |EffectsLoaderTestBase| is a test fixture that enables tests using the 'test_effects_v1.so'
// module. This module provides 2 exports; the standard Fuchsia Audio Effect ABI that allows the
// plugin to function with the Fuchsia Audio stack, and an additional 'test effects extension'
// ABI which is an ABI defined by the test_effects module to allow tests to control the behavior
// of the Fuchsia Audio Effects implementation.
class EffectsLoaderV1TestBase : public ::testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  const TestEffectsV1Module& test_effects() { return test_effects_; }
  EffectsLoaderV1* effects_loader() { return effects_loader_.get(); }

  void RecreateLoader();

 private:
  std::unique_ptr<EffectsLoaderV1> effects_loader_;
  TestEffectsV1Module test_effects_ = TestEffectsV1Module::Open();
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_EFFECTS_LOADER_V1_TEST_BASE_H_
