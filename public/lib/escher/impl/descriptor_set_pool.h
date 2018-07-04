// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_DESCRIPTOR_SET_POOL_H_
#define LIB_ESCHER_IMPL_DESCRIPTOR_SET_POOL_H_

#include <vector>
#include <vulkan/vulkan.hpp>

#include "lib/escher/resources/resource.h"
#include "lib/escher/resources/resource_manager.h"

namespace escher {
namespace impl {

class CommandBuffer;
class DescriptorSetPool;

// Contains a vector of vk::DescriptorSets that were obtained from a
// DescriptorSetPool (see below).  When this object is destroyed, the sets are
// returned to the pool.  The primary use-case is for allocations to be retained
// by a CommandBuffer; by obtaining a new allocation from the DescriptorSetPool
// each frame, the application can guarantee that no descriptor set is written
// nor destroyed while it is still in use by the device.
class DescriptorSetAllocation : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  ~DescriptorSetAllocation() override;

  const vk::DescriptorSet& get(size_t index) const { return sets_[index]; }
  vk::DescriptorSet* data() { return sets_.data(); }
  uint32_t size() { return static_cast<uint32_t>(sets_.size()); }

 private:
  friend class DescriptorSetPool;
  DescriptorSetAllocation(DescriptorSetPool* pool,
                          std::vector<vk::DescriptorSet> descriptor_sets);

  std::vector<vk::DescriptorSet> sets_;
};

typedef fxl::RefPtr<DescriptorSetAllocation> DescriptorSetAllocationPtr;

// Interface that allows acquisition of DescriptorSets for a single use within
// a particular CommandBuffer.  When that CommandBuffer is retired, all such
// DescriptorSets are returned to the pool from which they originated, so that
// they can be reused.
class DescriptorSetPool : public ResourceManager {
 public:
  DescriptorSetPool(EscherWeakPtr escher,
                    const vk::DescriptorSetLayoutCreateInfo& layout_info,
                    uint32_t initial_capacity = 10);
  ~DescriptorSetPool();

  // Allocate the requested number of descriptor sets, returning them in the
  // form of a DescriptorSetAllocation.  All such allocations must be destroyed
  // before this DescriptorSetPool is destroyed.  If command_buffer is not null,
  // it will retain the new allocation until it is retired.
  DescriptorSetAllocationPtr Allocate(uint32_t count,
                                      CommandBuffer* command_buffer);

  vk::DescriptorSetLayout layout() const { return layout_; }

 private:
  // Implement Owner::OnReceiveOwnable().
  void OnReceiveOwnable(std::unique_ptr<Resource> resource) override;

  // Create a new vk::DescriptorPool, and use it to allocate the specified
  // number of vk::DescriptorSets, which are then added to free_sets_.
  void InternalAllocate(uint32_t descriptor_set_count);

  // These are used each time that more descriptor sets must be allocated.
  vk::DescriptorSetLayout layout_;
  std::vector<vk::DescriptorPoolSize> descriptor_counts_;

  // Sets that are free to be used in a new allocation.
  std::vector<vk::DescriptorSet> free_sets_;
  std::vector<vk::DescriptorPool> pools_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DescriptorSetPool);
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_DESCRIPTOR_SET_POOL_H_
