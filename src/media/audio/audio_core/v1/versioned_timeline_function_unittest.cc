// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/versioned_timeline_function.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

TEST(VersionedTimelineFunctionTest, UpdateIncrementsGeneration) {
  TimelineFunction function1 = TimelineFunction(1, 1, 1, 1);
  TimelineFunction function2 = TimelineFunction(1, 2, 1, 1);
  VersionedTimelineFunction under_test(function1);

  auto [_, initial_generation] = under_test.get();

  // Update with the same function, should not increment generation.
  under_test.Update(function1);
  {
    auto [_, gen] = under_test.get();
    EXPECT_EQ(gen, initial_generation);
  }

  // Update function, increment generation.
  under_test.Update(function2);
  {
    auto [_, gen] = under_test.get();
    EXPECT_GT(gen, initial_generation);
  }
}

}  // namespace
}  // namespace media::audio
