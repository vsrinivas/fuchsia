// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_TEST_BASE_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_TEST_BASE_H_

#include <dlfcn.h>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/effects/test_effects/test_effects.h"
#include "src/media/audio/lib/effects_loader/effects_processor.h"

namespace media::audio::test {

static constexpr char kTestEffectsModuleName[] = "test_effects.so";

// The |EffectsLoaderTestBase| is a test fixture that enables tests using the 'test_effects.so'
// module. This module provides 2 exports; the standard Fuchsia Audio Effect ABI that allows the
// plugin to function with the Fuchsia Audio stack, and an additional 'test effects extension'
// ABI which is an ABI defined by the test_effects module to allow tests to control the behavior
// of the Fuchsia Audio Effects implementation.
class EffectsLoaderTestBase : public testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

  test_effects_module_ext* test_effects() { return test_effects_.get(); }
  EffectsLoader* effects_loader() { return &effects_loader_; }

 private:
  EffectsLoader effects_loader_{kTestEffectsModuleName};
  std::shared_ptr<test_effects_module_ext> test_effects_;
};

// Opens the 'extension' interface to the test_effects module. This is an auxiliary ABI in addition
// to the Fuchsia Effects ABI that allows the behavior of the test_effects module to be controlled
// by tests.
std::shared_ptr<test_effects_module_ext> OpenTestEffectsExt();

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_LOADER_TEST_BASE_H_
