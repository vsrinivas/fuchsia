// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "growable_slab.h"

#include <ostream>
#include <unordered_map>
#include <unordered_set>

#include <gtest/gtest.h>

#define ASSERT_OK(v) ASSERT_EQ(v, ZX_OK)

namespace vmo_store {
namespace testing {

class SimpleType {
 public:
  SimpleType(uint32_t v) : value_(v) {}

  bool operator==(const SimpleType& rhs) const { return value_ == rhs.value_; }

  bool operator!=(const SimpleType& rhs) const { return !(rhs == *this); }

  friend std::ostream& operator<<(std::ostream& os, const SimpleType& type) {
    os << "SimpleType(" << type.value_ << ")";
    return os;
  }

  uint32_t get() const { return value_; }

 private:
  uint32_t value_;
};

class MoveOnlyType {
 public:
  MoveOnlyType(uint32_t v) : value_(v) {}
  MoveOnlyType(MoveOnlyType&& other) {
    value_ = other.value_;
    other.value_ = UINT32_MAX;
  };

  MoveOnlyType(const MoveOnlyType&) = delete;
  MoveOnlyType& operator=(const MoveOnlyType& other) = delete;

  bool operator==(const MoveOnlyType& rhs) const { return value_ == rhs.value_; }

  bool operator!=(const MoveOnlyType& rhs) const { return !(rhs == *this); }

  friend std::ostream& operator<<(std::ostream& os, const MoveOnlyType& type) {
    os << "MoveOnlyType(" << type.value_ << ")";
    return os;
  }

  uint32_t get() const { return value_; }

 private:
  uint32_t value_;
};

template <typename T>
class GrowableSlabTest : public ::testing::Test {};
struct SimpleSize {
  using Key = size_t;
  using Value = SimpleType;
};
struct SimpleU32 {
  using Key = uint32_t;
  using Value = SimpleType;
};
struct MoveSize {
  using Key = size_t;
  using Value = MoveOnlyType;
};
struct MoveU32 {
  using Key = uint32_t;
  using Value = MoveOnlyType;
};

using TestTypes = ::testing::Types<SimpleSize, SimpleU32, MoveSize, MoveU32>;
TYPED_TEST_SUITE(GrowableSlabTest, TestTypes);

TYPED_TEST(GrowableSlabTest, Capacity) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  ASSERT_EQ(slab.capacity(), 0u);
  ASSERT_EQ(slab.count(), 0u);
  ASSERT_OK(slab.GrowTo(50));
  ASSERT_EQ(slab.capacity(), 50u);
  ASSERT_EQ(slab.free(), 50u);
  ASSERT_EQ(slab.count(), 0u);
  ASSERT_OK(slab.GrowTo(20));
  ASSERT_EQ(slab.capacity(), 50u);
}

TYPED_TEST(GrowableSlabTest, PushGet) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  using Key = typename TypeParam::Key;
  constexpr Key kCapacity = 3;
  ASSERT_OK(slab.GrowTo(kCapacity));
  for (Key i = 0; i < kCapacity; i++) {
    auto key = slab.Push(i + 10);
    ASSERT_TRUE(key.has_value());
    ASSERT_EQ(slab.capacity(), kCapacity);
    ASSERT_EQ(slab.count(), i + 1);
    ASSERT_EQ(slab.free(), kCapacity - i - 1);

    auto* value = slab.Get(*key);
    ASSERT_TRUE(value);
    ASSERT_EQ(*value, i + 10);
  }
}

TYPED_TEST(GrowableSlabTest, PushNoSpace) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  using Key = typename TypeParam::Key;
  constexpr Key kCapacity = 3;
  ASSERT_OK(slab.GrowTo(kCapacity));
  for (Key i = 0; i < kCapacity; i++) {
    auto key = slab.Push(i + 10);
    ASSERT_TRUE(key.has_value());
  }
  auto key = slab.Push(1000);
  ASSERT_FALSE(key.has_value()) << "Key has unexpected value " << *key;
}

TYPED_TEST(GrowableSlabTest, Free) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  using Key = typename TypeParam::Key;
  constexpr Key kCapacity = 3;
  ASSERT_OK(slab.GrowTo(kCapacity));
  std::vector<std::tuple<Key, uint32_t>> keys;
  for (Key i = 0; i < kCapacity; i++) {
    uint32_t value = i + 10;
    auto key = slab.Push(value);
    ASSERT_TRUE(key.has_value());
    keys.emplace_back(*key, value);
  }
  Key expect_free = 0;
  ASSERT_EQ(slab.free(), 0u);
  for (const auto& [k, v] : keys) {
    auto removed = slab.Erase(k);
    expect_free++;
    ASSERT_TRUE(removed.has_value());
    ASSERT_EQ(*removed, v);
    ASSERT_EQ(slab.free(), expect_free);
    ASSERT_EQ(slab.count(), kCapacity - expect_free);
  }
  // Check bad frees (including a key equal to capacity and one over it).
  keys.emplace_back(kCapacity, 0);
  keys.emplace_back(kCapacity + 10, 0);
  for (auto& [k, _v] : keys) {
    auto removed = slab.Erase(k);
    ASSERT_FALSE(removed.has_value()) << "Unexpected remove value " << *removed << " on key " << k;
    ASSERT_EQ(slab.free(), kCapacity);
  }
}

TYPED_TEST(GrowableSlabTest, PushFreeGet) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  using Key = typename TypeParam::Key;
  constexpr Key kCapacity = 15;
  ASSERT_OK(slab.GrowTo(kCapacity));
  std::vector<std::tuple<Key, uint32_t>> keys;
  for (Key i = 0; i < kCapacity; i++) {
    uint32_t value = i + 10;
    auto key = slab.Push(value);
    ASSERT_TRUE(key.has_value());
    keys.emplace_back(*key, value);
  }
  ASSERT_EQ(slab.count(), kCapacity);
  ASSERT_EQ(slab.free(), 0u);
  // Remove all odd values.
  Key remove_count = 0;
  for (const auto& [k, v] : keys) {
    if (v & 1u) {
      auto removed = slab.Erase(k);
      ASSERT_TRUE(removed.has_value());
      ASSERT_EQ(*removed, v);
      remove_count++;
    }
  }
  ASSERT_EQ(slab.count(), kCapacity - remove_count);
  ASSERT_EQ(slab.free(), remove_count);
  // Check that we can get only the keys that still exist.
  for (auto& [k, v] : keys) {
    auto* value = slab.Get(k);
    if (v & 1u) {
      // Odd value was removed.
      ASSERT_TRUE(value == nullptr) << "Unexpected valid value: " << *value << " for key " << k;
    } else {
      ASSERT_TRUE(value != nullptr);
      ASSERT_EQ(*value, v);
    }
  }
  // Reinsert the removed keys.
  for (auto& [k, v] : keys) {
    if (v & 1u) {
      auto key = slab.Push(v);
      ASSERT_TRUE(key.has_value());
      k = *key;
    }
  }
  ASSERT_EQ(slab.count(), kCapacity);
  ASSERT_EQ(slab.free(), 0u);
  // Get all values again and check everything is in order.
  for (const auto& [k, v] : keys) {
    auto* value = slab.Get(k);
    ASSERT_TRUE(value != nullptr);
    ASSERT_EQ(*value, v);
  }
}

TYPED_TEST(GrowableSlabTest, Insert) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  using Key = typename TypeParam::Key;
  constexpr Key kCapacity = 7;
  constexpr Key kReservedKey = 4;
  ASSERT_OK(slab.GrowTo(kCapacity));
  // All the keys up to capacity should be available, we'll use all of them but one and push at the
  // end.
  for (Key i = 0; i < kCapacity; i++) {
    if (i != kReservedKey) {
      ASSERT_OK(slab.Insert(i, i + 10));
    }
  }
  ASSERT_EQ(slab.count(), kCapacity - 1);
  ASSERT_EQ(slab.free(), 1u);
  auto key = slab.Push(999);
  ASSERT_TRUE(key.has_value());
  ASSERT_EQ(*key, kReservedKey);
  ASSERT_EQ(slab.free(), 0u);
  ASSERT_EQ(slab.count(), kCapacity);
  // Inserting a key equal to or greater than capacity is invalid.
  ASSERT_EQ(slab.Insert(kCapacity, 1), ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(slab.Insert(kCapacity + 10, 1), ZX_ERR_OUT_OF_RANGE);
  // Inserting a key that is already occupied is also invalid.
  ASSERT_EQ(slab.Insert(0, 1), ZX_ERR_ALREADY_EXISTS);
  ASSERT_TRUE(slab.Erase(0).has_value());
  ASSERT_OK(slab.Insert(0, 1));
}

TYPED_TEST(GrowableSlabTest, Grow) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  ASSERT_EQ(slab.capacity(), 0u);
  ASSERT_OK(slab.Grow());
  ASSERT_EQ(slab.capacity(), 1u);
  ASSERT_OK(slab.Grow());
  // Doesn't grow if we still have free slots.
  ASSERT_EQ(slab.capacity(), 1u);
  ASSERT_TRUE(slab.Push(1).has_value());
  ASSERT_OK(slab.Grow());
  ASSERT_EQ(slab.capacity(), 2u);
  while (slab.free() != 0) {
    ASSERT_TRUE(slab.Push(1).has_value());
  }
  ASSERT_OK(slab.Grow());
  ASSERT_EQ(slab.capacity(), 4u);
}

TYPED_TEST(GrowableSlabTest, Iterator) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  using Key = typename TypeParam::Key;
  constexpr Key kCapacity = 15;
  ASSERT_OK(slab.GrowTo(kCapacity));
  std::vector<std::pair<Key, uint32_t>> inserted;
  ASSERT_EQ(slab.begin(), slab.end());
  for (Key i = 0; i < kCapacity; i++) {
    auto value = static_cast<uint32_t>(i + 10);
    auto key = slab.Push(value);
    ASSERT_TRUE(key.has_value());
    inserted.emplace_back(*key, value);
  }
  ASSERT_NE(slab.begin(), slab.end());

  // Iterate over the slab and the vector to match the ordering of the values.
  auto it = inserted.begin();
  for (const auto& i : slab) {
    ASSERT_NE(it, inserted.end());
    ASSERT_EQ(i.get(), it->second);
    it++;
  }
  ASSERT_EQ(it, inserted.end());

  // Remove all keys multiple of 3 from both the slab and the vector.
  Key removed_count = 0;
  for (auto i = inserted.begin(); i != inserted.end();) {
    auto& [k, v] = *i;
    if (v % 3 == 0) {
      auto removed = slab.Erase(k);
      ASSERT_TRUE(removed.has_value());
      ASSERT_EQ(*removed, v);
      removed_count++;
      i = inserted.erase(i);
    } else {
      i++;
    }
  }

  // Iterate again and check that the iterator is still sane.
  it = inserted.begin();
  for (const auto& i : slab) {
    ASSERT_NE(it, inserted.end());
    ASSERT_EQ(i.get(), it->second);
    it++;
  }
  ASSERT_EQ(it, inserted.end());
}

TYPED_TEST(GrowableSlabTest, NoFastReuse) {
  // Tests that the free list in the slab is a queue and not a stack, delaying reuse of old keys.
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  ASSERT_OK(slab.GrowTo(3));
  auto key1 = slab.Push(1);
  ASSERT_TRUE(key1.has_value());
  auto key2 = slab.Push(2);
  ASSERT_TRUE(key2.has_value());
  ASSERT_NE(*key1, *key2);
  // Free key 1, and push a new value, assert that key1 is not immediately reused.
  ASSERT_TRUE(slab.Erase(*key1).has_value());
  auto key3 = slab.Push(3);
  ASSERT_TRUE(key3.has_value());
  ASSERT_NE(*key3, *key1);
  ASSERT_NE(*key3, *key2);
}

TYPED_TEST(GrowableSlabTest, Clear) {
  GrowableSlab<typename TypeParam::Value, typename TypeParam::Key> slab;
  using Key = typename TypeParam::Key;
  constexpr Key kCapacity = 15;
  ASSERT_OK(slab.GrowTo(kCapacity));
  while (slab.free() != 0) {
    ASSERT_TRUE(slab.Push(1).has_value());
  }
  ASSERT_EQ(slab.count(), kCapacity);
  slab.Clear();
  ASSERT_EQ(slab.count(), 0u);
  ASSERT_EQ(slab.begin(), slab.end());
  ASSERT_EQ(slab.free(), kCapacity);
}

TEST(MiscTest, DestructorIsCalled) {
  // A class that increments a counter on construction and decrements on destruction. We'll use it
  // to make sure the destructors get called as expected.
  class Value {
   public:
    explicit Value(size_t* counter) : counter_(counter) { *counter_ += 1; }
    ~Value() {
      if (counter_) {
        *counter_ -= 1;
      }
    }
    Value(Value&& other) noexcept {
      counter_ = other.counter_;
      other.counter_ = nullptr;
    }

   private:
    size_t* counter_;
  };
  GrowableSlab<Value> slab;
  constexpr size_t kCapacity = 6;
  size_t counter = 0;
  ASSERT_OK(slab.GrowTo(kCapacity));
  std::vector<size_t> keys;
  while (slab.free() != 0) {
    auto key = slab.Push(Value(&counter));
    ASSERT_TRUE(key.has_value());
    keys.push_back(*key);
  }
  ASSERT_EQ(counter, kCapacity);
  for (size_t i = 0; i < kCapacity / 2; i++) {
    slab.Erase(keys[i]);
    ASSERT_EQ(counter, kCapacity - i - 1u);
  }
  slab.Clear();
  ASSERT_EQ(counter, 0u);
}

}  // namespace testing
}  // namespace vmo_store
