// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/hash_cache.h"

#include "gtest/gtest.h"

namespace {
using namespace escher;

class TestObj : public HashCacheItem<TestObj> {
 public:
  TestObj(size_t id) : id_(id) {}

  size_t id() const { return id_; }

 private:
  size_t id_;
};

// Block-based allocation policy.  This could be written more straightforwardly,
// but is written this way to explore the nuances of policies that clients might
// want to provide.
class TestObjObjectPoolPolicy {
 public:
  TestObjObjectPoolPolicy() {}
  ~TestObjObjectPoolPolicy() {
    EXPECT_EQ(init_object_count_, destroy_object_count_);
    EXPECT_EQ(init_block_count_, destroy_block_count_);
  }

  // Objects are constructed in InitializePoolObjectBlock(), so there is no need
  // to do it here.  See DescriptorSetAllocator for a real-life example of why
  // a client might create/destroy blocks of objects, rather than each object
  // separately.
  inline void InitializePoolObject(TestObj* ptr) { ++init_object_count_; }

  // HashCache conveniently wraps whatever policy is provided to it with a
  // HashCacheObjectPoolPolicy.  Its version of DestroyPoolObject() doesn't
  // call the destructor (because then the object state would be undefined the
  // next time InitializePoolObject() is called), but it does clear the various
  // fields used by the HashCache implementation.
  inline void DestroyPoolObject(TestObj* ptr) { ++destroy_object_count_; }

  // Construct an entire block of objects.
  void InitializePoolObjectBlock(TestObj* objects, size_t block_index,
                                 size_t num_objects) {
    ++init_block_count_;

    ASSERT_EQ(num_objects, ObjectPool<TestObj>::NumObjectsInBlock(block_index));

    size_t i, base_index = 0;
    for (i = 0; i < block_index; ++i) {
      base_index += ObjectPool<TestObj>::NumObjectsInBlock(i);
    }

    for (i = 0; i < num_objects; ++i) {
      // Cause IDs to be contiguous when ObjectPool immediately adds these to
      // |vacants_|.
      new (objects + i) TestObj(base_index + num_objects - i - 1);
    }
  }

  // We don't actually call destructors for the TestObjs.  This is OK because we
  // know that TestObj doesn't hold any resouces that can be leaked.  For many
  // use-cases, this will not suffice.  For example, DescriptorSetAllocator
  // would leak Vulkan objects if it followed this approach.
  void DestroyPoolObjectBlock(TestObj* objects, size_t block_index,
                              size_t num_objects) {
    ++destroy_block_count_;
  }

  size_t init_object_count() const { return init_object_count_; }
  size_t destroy_object_count() const { return destroy_object_count_; }
  size_t init_block_count() const { return init_block_count_; }
  size_t destroy_block_count() const { return destroy_block_count_; }

 private:
  size_t init_object_count_ = 0;
  size_t destroy_object_count_ = 0;
  size_t init_block_count_ = 0;
  size_t destroy_block_count_ = 0;
};

// Helper function for the test-cases below.
template <class CacheT>
void ObtainAndValidateObjects(CacheT* cache, bool already_cached, size_t count,
                              size_t start_index = 0) {
  for (size_t i = start_index; i < start_index + count; ++i) {
    auto result = cache->Obtain({i});
    ASSERT_EQ(result.second, already_cached);
    ASSERT_EQ(i, result.first->id());
  }
}

// Test HashCache with |FramesUntilEviction| == 0, which means that there is no
// frame-to-frame caching: objects are not cached even if they were used in the
// previous frame.
TEST(HashCache, NoFrameToFrameCaching) {
  constexpr size_t kCount = 512;
  constexpr size_t kFramesUntilEviction = 0;
  HashCache<TestObj, TestObjObjectPoolPolicy, kFramesUntilEviction> cache;
  auto& policy = cache.object_pool().policy();

  cache.BeginFrame();
  EXPECT_EQ(0U, policy.init_object_count());
  EXPECT_EQ(0U, policy.destroy_object_count());
  EXPECT_EQ(0U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());

  ObtainAndValidateObjects(&cache, false, kCount);
  EXPECT_EQ(kCount, policy.init_object_count());
  EXPECT_EQ(0U, policy.destroy_object_count());
  EXPECT_EQ(4U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());

  // Accessing the same keys as before will not increment the block count.
  ObtainAndValidateObjects(&cache, true, kCount);
  ObtainAndValidateObjects(&cache, true, kCount);
  EXPECT_EQ(kCount, policy.init_object_count());
  EXPECT_EQ(0U, policy.destroy_object_count());
  EXPECT_EQ(4U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());

  // All the current objects are evicted from the cache upon BeginFrame().
  // However, the underlying memory is not freed; it remains available for
  // subsequent allocation requests.
  cache.BeginFrame();
  EXPECT_EQ(kCount, policy.init_object_count());
  EXPECT_EQ(kCount, policy.destroy_object_count());
  EXPECT_EQ(4U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());

  ObtainAndValidateObjects(&cache, false, kCount);
  EXPECT_EQ(2 * kCount, policy.init_object_count());
  EXPECT_EQ(kCount, policy.destroy_object_count());
  EXPECT_EQ(4U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());

  ObtainAndValidateObjects(&cache, true, kCount);
  ObtainAndValidateObjects(&cache, true, kCount);
  EXPECT_EQ(2 * kCount, policy.init_object_count());
  EXPECT_EQ(kCount, policy.destroy_object_count());
  EXPECT_EQ(4U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());

  // Clear() destroys all objects/blocks.  Don't do this until you are sure
  // that the objects are no longer being used.  For example, if the cache holds
  // Vulkan objects, don't destroy them while they are still being used to
  // render a frame.
  cache.Clear();
  EXPECT_EQ(2 * kCount, policy.init_object_count());
  EXPECT_EQ(2 * kCount, policy.destroy_object_count());
  EXPECT_EQ(4U, policy.init_block_count());
  EXPECT_EQ(4U, policy.destroy_block_count());

  ObtainAndValidateObjects(&cache, false, kCount);
  EXPECT_EQ(3 * kCount, policy.init_object_count());
  EXPECT_EQ(2 * kCount, policy.destroy_object_count());
  EXPECT_EQ(8U, policy.init_block_count());
  EXPECT_EQ(4U, policy.destroy_block_count());

  cache.Clear();
  EXPECT_EQ(3 * kCount, policy.init_object_count());
  EXPECT_EQ(3 * kCount, policy.destroy_object_count());
  EXPECT_EQ(8U, policy.init_block_count());
  EXPECT_EQ(8U, policy.destroy_block_count());
}

// Test HashCache with |FramesUntilEviction| == 2, which means that objects are
// cached even if they are not used for an entire frame, but they are flushed
// from the cache if not used for two entire frames.  In this variant, either
// all objects are used in a frame, or none are.
TEST(HashCache, FullFrameToFrameCaching) {
  constexpr size_t kCount = 512;
  constexpr size_t kFramesUntilEviction = 2;
  HashCache<TestObj, TestObjObjectPoolPolicy, kFramesUntilEviction> cache;

  auto& policy = cache.object_pool().policy();
  EXPECT_EQ(0U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());

  // First frame: nothing is cached at first.
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, false, kCount);
  ObtainAndValidateObjects(&cache, true, kCount);

  // Everything is still cached next frame.
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, true, kCount);

  // Everything is still cached even if nothing is used for a frame.
  cache.BeginFrame();
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, true, kCount);

  // If an item isn't used for two frames, it is evicted from the cache.
  cache.BeginFrame();
  cache.BeginFrame();
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, false, kCount);

  // Double-check that everything is cached if nothing is used for a frame, and
  // evicted after two frames.
  cache.BeginFrame();
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, true, kCount);
  cache.BeginFrame();
  cache.BeginFrame();
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, false, kCount);

  // Cache's ObjectPool doesn't release underlying resources until the entire
  // cache is cleared.
  EXPECT_EQ(4U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());
  cache.Clear();
  EXPECT_EQ(4U, policy.init_block_count());
  EXPECT_EQ(4U, policy.destroy_block_count());
}

// Test HashCache with |FramesUntilEviction| == 2, which means that objects are
// cached even if they are not used for an entire frame, but they are flushed
// from the cache if not used for two entire frames.  In this variant, only some
// objects are used each frame.
TEST(HashCache, PartialFrameToFrameCaching) {
  constexpr size_t kCount = 512;
  constexpr size_t kFramesUntilEviction = 2;
  HashCache<TestObj, TestObjObjectPoolPolicy, kFramesUntilEviction> cache;

  auto& policy = cache.object_pool().policy();
  EXPECT_EQ(0U, policy.init_block_count());
  EXPECT_EQ(0U, policy.destroy_block_count());

  // First frame: nothing is cached at first.
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, false, kCount, 0);
  ObtainAndValidateObjects(&cache, true, kCount, 0);
  ObtainAndValidateObjects(&cache, false, kCount, kCount);
  ObtainAndValidateObjects(&cache, true, kCount, kCount);

  // Use half of the objects next frame.  They should still be cached.
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, true, kCount, 0);

  // Use the other half of the objects next frame.  They should still be cached.
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, true, kCount, kCount);

  // Skip a frame.  Only half of the objects should still be cached.
  cache.BeginFrame();
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, false, kCount, 0);
  ObtainAndValidateObjects(&cache, true, kCount, kCount);

  // Skip two frames.  No objects should still be cached.
  cache.BeginFrame();
  cache.BeginFrame();
  cache.BeginFrame();
  ObtainAndValidateObjects(&cache, false, 2 * kCount, 0);
}

}  // namespace
