// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMPL_DESCRIPTOR_SET_ALLOCATOR_CACHE_H_
#define SRC_UI_LIB_ESCHER_VK_IMPL_DESCRIPTOR_SET_ALLOCATOR_CACHE_H_

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/util/hash_map.h"
#include "src/ui/lib/escher/vk/impl/descriptor_set_allocator.h"

namespace escher {
namespace impl {

// Lazily creates and caches DescriptorSetAllocators upon demand.
// DescriptorSetAllocator instances are kept alive by std::shared_ptr held by PipelineLayout.
class DescriptorSetAllocatorCache {
 public:
  explicit DescriptorSetAllocatorCache(vk::Device device);
  ~DescriptorSetAllocatorCache() = default;

  DescriptorSetAllocatorPtr ObtainDescriptorSetAllocator(const DescriptorSetLayout& layout,
                                                         const SamplerPtr& immutable_sampler);

  // Cycles through |descriptor_set_allocators_| to filter out the ones that are deleted and calls
  // BeginFrame() on the ones alive to signal the start of a new lifetime cycle.
  void BeginFrame();

  // Return the number of allocators in the cache.
  size_t size() const { return descriptor_set_allocators_.size(); }

 private:
  vk::Device device_;

  HashMap<Hash, std::weak_ptr<DescriptorSetAllocator>> descriptor_set_allocators_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_IMPL_DESCRIPTOR_SET_ALLOCATOR_CACHE_H_
