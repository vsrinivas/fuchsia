// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_DESCRIPTOR_SET_ALLOCATOR_H_
#define LIB_ESCHER_VK_DESCRIPTOR_SET_ALLOCATOR_H_

#include <map>
#include <vulkan/vulkan.hpp>

#include "lib/escher/util/hash_cache.h"
#include "lib/escher/vk/descriptor_set_layout.h"

namespace escher {

// DescriptorSetAllocator wraps HashCache to provide a frame-based cache for
// Vulkan descriptor sets.  The eviction semantics are the same as a HashCache
// with FramesUntilEviction == 2.
class DescriptorSetAllocator {
 public:
  DescriptorSetAllocator(vk::Device device, DescriptorSetLayout layout);

  void BeginFrame() { cache_.BeginFrame(); }
  void Clear() { cache_.Clear(); }

  // Get the descriptor set corresponding to the hashed value.  The second
  // element of the pair is true if the descriptor set already contains valid
  // data, and false if new descriptor values must be written.
  std::pair<vk::DescriptorSet, bool> Get(Hash hash) {
    auto pair = cache_.Obtain(hash);
    return std::make_pair(pair.first->set, pair.second);
  }

  const DescriptorSetLayout& layout() const {
    return cache_.object_pool().policy().layout();
  }

  vk::DescriptorSetLayout vk_layout() const {
    return cache_.object_pool().policy().vk_layout();
  }

  size_t cache_hits() const { return cache_.cache_hits(); }
  size_t cache_misses() const { return cache_.cache_misses(); }

 private:
  // Items stored in |cache_|.
  struct CacheItem : public HashCacheItem<CacheItem> {
    vk::DescriptorSet set;
  };

  // Allocates blocks of vk::DescriptorSets, rather than allocating one at a
  // time. Each block is associated with a separate vk::DescriptorPool.
  class PoolPolicy {
   public:
    PoolPolicy(vk::Device device, DescriptorSetLayout layout);
    ~PoolPolicy();

    void InitializePoolObjectBlock(CacheItem* objects,
                                   size_t block_index,
                                   size_t num_objects);

    void DestroyPoolObjectBlock(CacheItem* objects,
                                size_t block_index,
                                size_t num_objects);

    inline void InitializePoolObject(CacheItem* ptr) {}
    inline void DestroyPoolObject(CacheItem* ptr) {}

    vk::Device vk_device() const { return vk_device_; }
    vk::DescriptorSetLayout vk_layout() const { return vk_layout_; }
    const DescriptorSetLayout& layout() const { return layout_; }

   private:
    // Helpers for InitializePoolObjectBlock().
    vk::DescriptorPool CreatePool(size_t block_index, size_t num_objects);
    void AllocateDescriptorSetBlock(vk::DescriptorPool pool,
                                    CacheItem* objects,
                                    size_t num_objects);

    vk::Device vk_device_;
    vk::DescriptorSetLayout vk_layout_;
    DescriptorSetLayout layout_;

    std::vector<vk::DescriptorPoolSize> pool_sizes_;
    std::map<size_t, vk::DescriptorPool> pools_;
  };

  HashCache<CacheItem, PoolPolicy, 2> cache_;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_DESCRIPTOR_SET_ALLOCATOR_H_
