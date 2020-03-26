// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/hashable.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/util/hash_map.h"
#include "src/ui/lib/escher/util/hasher.h"

namespace {
using namespace escher;

class TestHashable : public Hashable {
 public:
  int32_t number() const { return number_; }
  const std::string& name() const { return name_; }

  void set_number(int32_t number) {
    number_ = number;
    InvalidateHash();
  }

  void set_name(std::string name) {
    name_ = std::move(name);
    InvalidateHash();
  }

  bool operator==(const TestHashable& other) const {
    return hash() == other.hash() && number_ == other.number_ && name_ == other.name_;
  }
  bool operator!=(const TestHashable& other) const { return !(*this == other); }

  using Hashable::HasCachedHash;

 private:
  Hash GenerateHash() const override {
    Hasher h;
    h.i32(number_);
    h.string(name_);
    return h.value();
  }

  int32_t number_ = 0;
  std::string name_;
};

TEST(Hashable, Basics) {
  TestHashable orig;
  orig.set_number(-147);
  orig.set_name("Steve");
  EXPECT_FALSE(orig.HasCachedHash());

  TestHashable copy = orig;
  EXPECT_FALSE(copy.HasCachedHash());
  EXPECT_EQ(orig, copy);
  EXPECT_EQ(orig.number(), copy.number());
  EXPECT_EQ(orig.name(), copy.name());
  EXPECT_EQ(orig.hash(), copy.hash());

  // Comparing them triggered hash generation in both.
  EXPECT_TRUE(orig.HasCachedHash());
  EXPECT_TRUE(copy.HasCachedHash());

  // Comparing works when the first arg has a cached hash but not the second.
  // Afterward both do.
  copy.set_number(-147);
  EXPECT_FALSE(copy.HasCachedHash());
  EXPECT_EQ(orig, copy);
  EXPECT_TRUE(copy.HasCachedHash());

  // Comparing works when the second arg has a cached hash but not the first.
  // Afterward both do.
  orig.set_number(-147);
  EXPECT_FALSE(orig.HasCachedHash());
  EXPECT_EQ(orig, copy);
  EXPECT_TRUE(orig.HasCachedHash());

  // Changing the name makes them unequal.
  copy.set_name("Aparna");
  EXPECT_NE(orig, copy);
  EXPECT_EQ(orig.number(), copy.number());
  EXPECT_NE(orig.name(), copy.name());
  EXPECT_NE(orig.hash(), copy.hash());
}

TEST(Hashable, AsHashMapKey) {
  TestHashable steve;
  steve.set_number(-147);
  steve.set_name("Steve");

  TestHashable aparna;
  aparna.set_number(-1147);
  aparna.set_name("Aparna");

  EXPECT_FALSE(steve.HasCachedHash());
  EXPECT_FALSE(aparna.HasCachedHash());

  HashMap<TestHashable, std::string> map;
  map[steve] = steve.name();
  map[aparna] = aparna.name();

  // Searching the map for the insertion-keys triggers hash-generation.
  EXPECT_TRUE(steve.HasCachedHash());
  EXPECT_TRUE(aparna.HasCachedHash());

  for (auto& key_value : map) {
    // Hash-generation was triggered for keys inserted into the map.
    EXPECT_TRUE(key_value.first.HasCachedHash());

    // Names should match.
    EXPECT_EQ(key_value.first.name(), key_value.second);
  }

  // Double-check name matching.
  EXPECT_EQ(steve.name(), map[steve]);
  EXPECT_EQ(aparna.name(), map[aparna]);
  EXPECT_NE(steve.name(), aparna.name());
}

}  // namespace
