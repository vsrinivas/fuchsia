// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_EFFECTS_LOADER_TEST_BASE_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_EFFECTS_LOADER_TEST_BASE_H_

#include <gtest/gtest.h>

#include "src/media/audio/effects/test_effects/test_effects.h"
#include "src/media/audio/lib/effects_loader/effects_loader.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects.h"

namespace media::audio::testing {

// The |EffectsLoaderTestBase| is a test fixture that enables tests using the 'test_effects.so'
// module. This module provides 2 exports; the standard Fuchsia Audio Effect ABI that allows the
// plugin to function with the Fuchsia Audio stack, and an additional 'test effects extension'
// ABI which is an ABI defined by the test_effects module to allow tests to control the behavior
// of the Fuchsia Audio Effects implementation.
class EffectsLoaderTestBase : public ::testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  test_effects_module_ext* test_effects() { return test_effects_.get(); }
  EffectsLoader* effects_loader() { return effects_loader_.get(); }

  void RecreateLoader();

 private:
  std::unique_ptr<EffectsLoader> effects_loader_;
  std::shared_ptr<test_effects_module_ext> test_effects_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_EFFECTS_LOADER_TEST_BASE_H_
