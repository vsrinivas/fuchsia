// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/effects_loader/effects_module.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/effects_loader/testing/effects_loader_test_base.h"

namespace media::audio {
namespace {

TEST(EffectsModuleTest, OpenModule) {
  auto m = EffectsModuleV1::Open(testing::kTestEffectsModuleName);
  ASSERT_TRUE(m);
  ASSERT_EQ(0u, m->num_effects);
}

TEST(EffectsModuleTest, MoveModule) {
  auto m1 = EffectsModuleV1::Open(testing::kTestEffectsModuleName);
  ASSERT_TRUE(m1);

  auto m2 = std::move(m1);
  EXPECT_FALSE(m1);
  EXPECT_TRUE(m2);
}

}  // namespace
}  // namespace media::audio
