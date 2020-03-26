// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/ui/lib/escher/util/hash_map.h"
#include "src/ui/lib/escher/util/hasher.h"

namespace {
using namespace escher;

// Helper struct for "CollisionsOK" test below.  This is a key that is designed
// to intentionally cause hash collisions, simulating an uncommon-but-possible
// real-world case.
struct HashMapCollisionKey {
  int32_t hashed_int;
  float hashed_float;
  int32_t unhashed_int;

  bool operator==(const HashMapCollisionKey& other) const {
    return hashed_int == other.hashed_int && hashed_float == other.hashed_float &&
           unhashed_int == other.unhashed_int;
  }

  struct HashMapHasher {
    size_t operator()(const HashMapCollisionKey& key) const {
      ++hash_count;
      Hasher h;
      h.i32(key.hashed_int);
      h.f32(key.hashed_float);
      return h.value().val;
    }

    mutable size_t hash_count = 0;
  };
};

// Collisions aren't desirable for performance reasons, but they shouldn't be
// catastrophic.
TEST(HashMap, CollisionsOK) {
  HashMap<HashMapCollisionKey, int> map;

  HashMapCollisionKey key1, key2;
  const int32_t kVal1 = -64;
  const int32_t kVal2 = -128;

  key1.hashed_int = key2.hashed_int = 234673423;
  key1.hashed_float = key2.hashed_float = 998766543321.0012334;
  key1.unhashed_int = kVal1;
  key2.unhashed_int = kVal2;

  HashMapCollisionKey::HashMapHasher key_hasher;
  EXPECT_EQ(key_hasher(key1), key_hasher(key2));
  EXPECT_EQ(key_hasher.hash_count, 2U);

  map[key1] = kVal1;
  map[key2] = kVal2;
  EXPECT_EQ(map[key1], kVal1);
  EXPECT_EQ(map[key2], kVal2);
}

}  // namespace
