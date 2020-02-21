// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/versioned_timeline_function.h"

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

TEST(DerivedTimelineFunctionTest, UpdateIncrementsGeneration) {
  TimelineFunction function1 = TimelineFunction(1, 1, 1, 1);
  auto base = fbl::MakeRefCounted<VersionedTimelineFunction>(function1);

  TimelineFunction function2 = TimelineFunction(1, 2, 1, 1);
  auto under_test = fbl::MakeRefCounted<DerivedTimelineFunction>(base, function2);

  auto [f, generation] = under_test->get();
  ASSERT_EQ(f, TimelineFunction::Compose(function2, function1));

  // Update with the same function, should not increment generation.
  base->Update(function1);
  under_test->Update(function2);
  {
    auto [_, gen] = under_test->get();
    EXPECT_EQ(gen, generation);
  }

  // Update base function, increment generation.
  base->Update(function2);
  {
    auto [_, gen] = under_test->get();
    ASSERT_GT(gen, generation);
    generation = gen;
  }

  // Update derived function, increment generation.
  under_test->Update(function1);
  {
    auto [_, gen] = under_test->get();
    EXPECT_GT(gen, generation);
  }
}

}  // namespace
}  // namespace media::audio
