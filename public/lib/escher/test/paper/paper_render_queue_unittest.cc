// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/paper/paper_render_queue.h"

#include <glm/gtc/matrix_access.hpp>

#include "gtest/gtest.h"

namespace {
using namespace escher;

TEST(PaperRenderQueue, OpaqueSortKeyBits) {
  Hash dddd, bbbb;
  dddd.val = 0xdddddddddddddddd;
  bbbb.val = 0xbbbbbbbbbbbbbbbb;
  float depth = 11.2345f;

  auto key = PaperRenderQueue::SortKey::NewOpaque(dddd, bbbb, depth);

  EXPECT_EQ(0xdddd00000000bbbb, key.key() & 0xffff00000000ffff);
  EXPECT_EQ(depth, glm::uintBitsToFloat((key.key() >> 16) & 0xffffffff));
}

TEST(PaperRenderQueue, TranslucentSortKeyBits) {
  Hash dddd, bbbb;
  dddd.val = 0xdddddddddddddddd;
  bbbb.val = 0xbbbbbbbbbbbbbbbb;
  float depth = 11.2345f;

  auto key = PaperRenderQueue::SortKey::NewTranslucent(dddd, bbbb, depth);

  EXPECT_EQ(0x00000000ddddbbbb, key.key() & 0x00000000ffffffff);
  EXPECT_EQ(depth, glm::uintBitsToFloat((key.key() >> 32) ^ 0xffffffff));
}

TEST(PaperRenderQueue, SortKeyComparisons) {
  Hash low_hash, high_hash;
  low_hash.val = 0xaaaaaaaaaaaaaaaa;
  high_hash.val = 0xbbbbbbbbbbbbbbbb;
  float near_depth = 11.2345f;
  float far_depth = 22.6789f;

  using Key = PaperRenderQueue::SortKey;

  // For both opaque and translucent, all else being equal, a low hash is sorted
  // earlier than a high hash.
  EXPECT_LT(Key::NewOpaque(low_hash, low_hash, near_depth).key(),
            Key::NewOpaque(low_hash, high_hash, near_depth).key());
  EXPECT_LT(Key::NewOpaque(low_hash, low_hash, near_depth).key(),
            Key::NewOpaque(high_hash, low_hash, near_depth).key());
  EXPECT_LT(Key::NewTranslucent(low_hash, low_hash, near_depth).key(),
            Key::NewTranslucent(low_hash, high_hash, near_depth).key());
  EXPECT_LT(Key::NewTranslucent(low_hash, low_hash, near_depth).key(),
            Key::NewTranslucent(high_hash, low_hash, near_depth).key());

  // For both opaque and translucent, the pipeline hash is more important than
  // the draw hash.
  EXPECT_LT(Key::NewOpaque(low_hash, high_hash, near_depth).key(),
            Key::NewOpaque(high_hash, low_hash, near_depth).key());
  EXPECT_LT(Key::NewTranslucent(low_hash, high_hash, near_depth).key(),
            Key::NewTranslucent(high_hash, low_hash, near_depth).key());

  // For opaque, depth sorting is front-to-back (to reduce overdraw), and for
  // translucent it is back-to-front (necessary for correct rendering).
  EXPECT_LT(Key::NewOpaque(low_hash, low_hash, near_depth).key(),
            Key::NewOpaque(low_hash, low_hash, far_depth).key());
  EXPECT_LT(Key::NewTranslucent(low_hash, low_hash, far_depth).key(),
            Key::NewTranslucent(low_hash, low_hash, near_depth).key());

  // For translucent, depth sorting is most important (this is necessary for
  // correct rendering).
  EXPECT_LT(Key::NewTranslucent(low_hash, low_hash, far_depth).key(),
            Key::NewTranslucent(high_hash, high_hash, near_depth).key());

  // For opaque, sorting by pipeline is most important, then depth, then draw
  // hash.
  EXPECT_LT(Key::NewOpaque(low_hash, low_hash, far_depth).key(),
            Key::NewOpaque(high_hash, low_hash, near_depth).key());
  EXPECT_LT(Key::NewOpaque(low_hash, high_hash, near_depth).key(),
            Key::NewOpaque(low_hash, low_hash, far_depth).key());
}

}  // namespace
