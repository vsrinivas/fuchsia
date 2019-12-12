// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_H_

#include <memory>

#include "src/media/audio/effects/test_effects/test_effects.h"

namespace media::audio::testing {

static constexpr char kTestEffectsModuleName[] = "test_effects.so";

// Opens the 'extension' interface to the test_effects module. This is an auxiliary ABI in addition
// to the Fuchsia Effects ABI that allows the behavior of the test_effects module to be controlled
// by tests.
//
// To use this correctly, you must also have included //src/media/audio/effects/test_effects as a
// loadable_module in the test_package that is linking against this library.
//
// See //src/media/audio/lib/effects_loader:audio_effects_loader_unittests as an example.
std::shared_ptr<test_effects_module_ext> OpenTestEffectsExt();

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_TESTING_TEST_EFFECTS_H_
