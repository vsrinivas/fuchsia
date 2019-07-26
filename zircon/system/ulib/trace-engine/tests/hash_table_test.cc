// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

#include <array>
#include <string>

#include "../hash_table.h"

namespace trace {
namespace {

struct StringEntry : internal::SinglyLinkedListable<StringEntry> {
  StringEntry() : string(nullptr) {}
  StringEntry(const char* str) : string(str) {}
  const char* GetKey() const { return string; }
  static size_t GetHash(const char* key) { return reinterpret_cast<uintptr_t>(key); }

  const char* string;
};

struct ThreadEntry : internal::SinglyLinkedListable<ThreadEntry> {
  ThreadEntry() : koid(ZX_HANDLE_INVALID) {}
  ThreadEntry(zx_koid_t k) : koid(k) {}
  zx_koid_t GetKey() const { return koid; }
  static size_t GetHash(zx_koid_t koid) { return koid; }

  zx_koid_t koid;
};

template <typename NodeType>
class ListTestFixture : public zxtest::Test {
 public:
  internal::SinglyLinkedList<NodeType> list;

  void TearDown() override {
    // Make sure the list is cleared before it is destructed,
    // or we'll get an assert failure, which indicates a test failure,
    // but not obvious to debug.
    list.clear();
  }
};

using StringListTest = ListTestFixture<StringEntry>;
using ThreadListTest = ListTestFixture<ThreadEntry>;

template <typename KeyType, typename NodeType>
class HashTableTestFixture : public zxtest::Test {
 public:
  internal::HashTable<KeyType, NodeType> hashtab;

  void TearDown() override {
    // Make sure the table is cleared before it is destructed,
    // or we'll get an assert failure, which indicates a test failure,
    // but not obvious to debug.
    hashtab.clear();
  }
};

using StringHashTableTest = HashTableTestFixture<const char*, StringEntry>;
using ThreadHashTableTest = HashTableTestFixture<zx_koid_t, ThreadEntry>;

TEST_F(StringListTest, Api) {
  ASSERT_TRUE(list.is_empty());

  StringEntry foo{"foo"};
  list.push_front(&foo);
  ASSERT_FALSE(list.is_empty());
  ASSERT_EQ(list.head(), &foo);

  StringEntry bar{"bar"};
  list.push_front(&bar);
  ASSERT_EQ(list.head(), &bar);

  list.clear();
  ASSERT_TRUE(list.is_empty());
}

TEST_F(ThreadListTest, Api) {
  ASSERT_TRUE(list.is_empty());

  ThreadEntry foo{42};
  list.push_front(&foo);
  ASSERT_FALSE(list.is_empty());
  ASSERT_EQ(list.head(), &foo);

  ThreadEntry bar{43};
  list.push_front(&bar);
  ASSERT_EQ(list.head(), &bar);

  list.clear();
  ASSERT_TRUE(list.is_empty());
}

TEST_F(StringHashTableTest, Api) {
  ASSERT_TRUE(hashtab.is_empty());

  constexpr size_t kNumEntries = 1000;
  std::array<std::string, kNumEntries> strings;
  std::array<StringEntry, kNumEntries> entries;
  for (size_t i = 0; i < kNumEntries; ++i) {
    char buf[10];
    sprintf(buf, "%zu", i);
    strings[i] = buf;
    entries[i] = StringEntry{strings[i].c_str()};
    hashtab.insert(&entries[i]);
    ASSERT_EQ(hashtab.size(), i + 1);
    ASSERT_FALSE(hashtab.is_empty());
  }

  for (size_t i = 0; i < kNumEntries; ++i) {
    ASSERT_NE(hashtab.lookup(strings[i].c_str()), nullptr);
  }

  ASSERT_EQ(hashtab.lookup("not-present"), nullptr);

  hashtab.clear();
  ASSERT_EQ(hashtab.size(), 0u);
  ASSERT_TRUE(hashtab.is_empty());
}

TEST_F(ThreadHashTableTest, Koid) {
  ASSERT_TRUE(hashtab.is_empty());

  constexpr size_t kNumEntries = 1000;
  std::array<ThreadEntry, kNumEntries> entries;
  for (size_t i = 0; i < kNumEntries; ++i) {
    entries[i] = ThreadEntry(i + 1);
    hashtab.insert(&entries[i]);
    ASSERT_EQ(hashtab.size(), i + 1);
    ASSERT_FALSE(hashtab.is_empty());
  }

  for (size_t i = 0; i < kNumEntries; ++i) {
    ASSERT_NE(hashtab.lookup(i + 1), nullptr);
  }

  ASSERT_EQ(hashtab.lookup(ZX_KOID_INVALID), nullptr);

  hashtab.clear();
  ASSERT_EQ(hashtab.size(), 0u);
  ASSERT_TRUE(hashtab.is_empty());
}

}  // namespace
}  // namespace trace
