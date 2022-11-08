// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/pin_executable_memory.h"

#include <gtest/gtest.h>

namespace media::audio::test {

TEST(PinExecutableMemory, DoesNotCrash) {
  // Verify we can pin executable memory without crashing.
  PinExecutableMemory::Singleton().Pin();
}

}  // namespace media::audio::test
