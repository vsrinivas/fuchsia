// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"

namespace scenic_impl {
namespace gfx {
namespace test {
namespace {

using namespace testing;

TEST(ViewHitAccumulatorTest, Empty) {
  ViewHitAccumulator accumulator;
  accumulator.EndLayer();
  EXPECT_TRUE(accumulator.hits().empty());
}

TEST(ViewHitAccumulatorTest, TopHitInASession) {
  ViewHitAccumulator accumulator;
  zx_koid_t view_ref_koid = 1u;

  accumulator.Add({.view_ref_koid = view_ref_koid, .distance = 2});
  accumulator.Add({.view_ref_koid = view_ref_koid, .distance = 1});
  accumulator.Add({.view_ref_koid = view_ref_koid, .distance = 3});
  accumulator.EndLayer();

  EXPECT_THAT(accumulator.hits(), ElementsAre(Field(&ViewHit::distance, 1)));
}

MATCHER(ViewIdEq, "view ID equals") {
  const auto& [hit, n] = arg;
  return hit.view_ref_koid == n;
}

TEST(ViewHitAccumulatorTest, SortedHitsPerLayer) {
  ViewHitAccumulator accumulator;
  zx_koid_t v1 = 1u, v2 = 2u, v3 = 3u;

  // Add hits in two layers to make sure we sort each one independently.

  accumulator.Add({.view_ref_koid = v1, .distance = 2});
  accumulator.Add({.view_ref_koid = v2, .distance = 1});
  accumulator.Add({.view_ref_koid = v3, .distance = 3});
  accumulator.EndLayer();

  accumulator.Add({.view_ref_koid = v1, .distance = 2});
  accumulator.Add({.view_ref_koid = v2, .distance = 3});
  accumulator.Add({.view_ref_koid = v3, .distance = 1});
  accumulator.EndLayer();

  EXPECT_THAT(accumulator.hits(), testing::Pointwise(ViewIdEq(), {2u, 1u, 3u, 3u, 1u, 2u}));
}

}  // namespace
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
