// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/epsilon_compare.h"

#include <limits>

#include <gtest/gtest.h>

namespace {
using namespace escher;

TEST(epsilon_compare, CompareFloat) {
  EXPECT_TRUE(CompareFloat(42, 42, 0));
  EXPECT_TRUE(CompareFloat(-.25f, -.125f, .125f));
  // experimentally shown to require an epsilon
  EXPECT_TRUE(CompareFloat(.9f, 9 * .1f, std::numeric_limits<float>::epsilon()));
  EXPECT_FALSE(CompareFloat(1, 2));
}

TEST(epsilon_compare, CompareMatrix) {
  EXPECT_TRUE(CompareMatrix(glm::mat4{1}, glm::mat4{1}, 0));
  // clang-format off
  EXPECT_TRUE(CompareMatrix(glm::mat4{0,     -.25f,  -.25f,   -1.125f,
                                      .25f,  1,      -1,      -.125f,
                                      .25f,  1,      2,       -.375f,
                                      1.125f, .125f, .375f,   3},
                            glm::mat4{0,     -.125f, -.375f,  -1,
                                      .125f, 1,      -1.125f, -.25f,
                                      .375f, 1.125f, 2,       -.25f,
                                      1,     .25f,   .25f,    3},
                            .125f));
  // clang-format on
  EXPECT_FALSE(CompareMatrix(glm::mat4{0}, glm::mat4{1}));
  // clang-format off
  EXPECT_FALSE(CompareMatrix(glm::mat4{0},
                             glm::mat4{0, 0, 0, 0,
                                       0, 0, 0, 0,
                                       0, 0, 0, 0,
                                       0, 0, 0, 1}));
  // clang-format on
}

}  // namespace
