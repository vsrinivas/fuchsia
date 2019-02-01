// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/object_pool.h"

#include "gtest/gtest.h"

namespace {
using namespace escher;

class TestObj {
 public:
  TestObj(size_t* leak_count, size_t one, size_t two = 5)
      : leak_count_(leak_count), one_(one), two_(two) {
    ++*leak_count_;
  }
  ~TestObj() { --*leak_count_; }

  size_t one() const { return one_; }
  size_t two() const { return two_; }

 private:
  size_t* const leak_count_;
  size_t one_;
  size_t two_;
};

// Test DefaultObjectPoolPolicy (i.e. what you get when you create an
// ObjectPool without a second template parameter).
TEST(ObjectPool, DefaultPolicy) {
  size_t leak_count = 0;
  ObjectPool<TestObj> pool;
  std::vector<TestObj*> allocated;
  constexpr size_t kAllocated = 512;
  for (size_t i = 0; i < kAllocated; ++i) {
    // Allocate a new TestObj, constructed with args |one| == i and |two| ==
    // kAllocated - i.  These will always sum to kAllocated; we test this below.
    allocated.push_back(pool.Allocate(&leak_count, i, kAllocated - i));
  }
  EXPECT_EQ(leak_count, kAllocated);

  while (!allocated.empty()) {
    TestObj* obj = allocated.back();
    EXPECT_EQ(kAllocated, obj->one() + obj->two());
    pool.Free(obj);
    allocated.pop_back();
    EXPECT_EQ(allocated.size(), pool.UnfreedObjectCount());
  }
  EXPECT_EQ(leak_count, 0U);
}

class TestObjPreinitializePolicy {
 public:
  TestObjPreinitializePolicy(size_t* leak_count) : leak_count_(leak_count) {}

  void InitializePoolObject(TestObj* ptr) {}
  void DestroyPoolObject(TestObj* ptr) {}
  void InitializePoolObjectBlock(TestObj* objects, size_t block_index,
                                 size_t num_objects) {
    for (size_t i = 0; i < num_objects; ++i) {
      new (objects + i) TestObj(leak_count_, block_index, i);
    }
  }
  void DestroyPoolObjectBlock(TestObj* objects, size_t block_index,
                              size_t num_objects) {
    for (size_t i = 0; i < num_objects; ++i) {
      EXPECT_EQ(objects[i].one(), block_index);
      EXPECT_EQ(objects[i].two(), i);

      objects[i].~TestObj();
    }
  }

 private:
  size_t* const leak_count_;
};

// Test an ObjectPool policy that preinitializes blocks of objects.
TEST(ObjectPool, PreinitializePolicy) {
  size_t leak_count = 0;
  ObjectPool<TestObj, TestObjPreinitializePolicy> pool(&leak_count);
  std::vector<TestObj*> allocated;
  constexpr size_t kAllocated = 512;
  for (size_t i = 0; i < kAllocated; ++i) {
    // NOTE: no args required to allocation, because the object is already
    // constructed by TestObjPreinitializePolicy::InitializePoolObjectBlock().
    allocated.push_back(pool.Allocate());
  }

  // Verify that the expected number of TestObjs were initialized.
  // NOTE: this is greater than than number allocated from the pool, because
  // this policy pre-initializes all TestObjs when their underlying memory
  // block is created.
  {
    size_t total_size = 0;
    size_t block_index = 0;
    while (total_size < kAllocated) {
      total_size +=
          ObjectPool<TestObj, TestObjPreinitializePolicy>::NumObjectsInBlock(
              block_index++);
    }
    EXPECT_GT(leak_count, kAllocated);
    EXPECT_EQ(leak_count, total_size);
  }

  while (!allocated.empty()) {
    TestObj* obj = allocated.back();
    pool.Free(obj);
    allocated.pop_back();
    EXPECT_EQ(allocated.size(), pool.UnfreedObjectCount());
  }
  // The objects aren't actually destroyed until the pool is cleared/destroyed.
  EXPECT_NE(leak_count, 0U);
  pool.Clear();
  EXPECT_EQ(leak_count, 0U);
}

}  // namespace
