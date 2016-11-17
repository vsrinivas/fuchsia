// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/descriptor_set_pool.h"

#include <map>

#include "escher/impl/command_buffer.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

DescriptorSetAllocation::DescriptorSetAllocation(
    DescriptorSetPool* pool,
    std::vector<vk::DescriptorSet> descriptor_sets)
    : Resource(nullptr), pool_(pool), sets_(std::move(descriptor_sets)) {}

DescriptorSetAllocation::~DescriptorSetAllocation() {
  pool_->ReturnDescriptorSets(std::move(sets_));
}

DescriptorSetPool::DescriptorSetPool(
    vk::Device device,
    const vk::DescriptorSetLayoutCreateInfo& layout_info,
    uint32_t initial_capacity)
    : device_(device),
      layout_(ESCHER_CHECKED_VK_RESULT(
          device_.createDescriptorSetLayout(layout_info))) {
  std::map<vk::DescriptorType, uint32_t> descriptor_type_counts;
  for (uint32_t i = 0; i < layout_info.bindingCount; ++i) {
    descriptor_type_counts[layout_info.pBindings[i].descriptorType] +=
        layout_info.pBindings[i].descriptorCount;
  }
  descriptor_counts_.reserve(descriptor_type_counts.size());
  for (auto& pair : descriptor_type_counts) {
    vk::DescriptorPoolSize dps;
    dps.type = pair.first;
    dps.descriptorCount = pair.second;
    descriptor_counts_.push_back(dps);
  }

  InternalAllocate(initial_capacity);
}

DescriptorSetPool::~DescriptorSetPool() {
  FTL_DCHECK(allocation_count_ == 0);
  for (auto pool : pools_) {
    device_.resetDescriptorPool(pool);
    device_.destroyDescriptorPool(pool);
  }
  device_.destroyDescriptorSetLayout(layout_);
}

DescriptorSetAllocationPtr DescriptorSetPool::Allocate(
    uint32_t count,
    CommandBuffer* command_buffer) {
  // Ensure that enough free sets are available.
  if (free_sets_.size() < count) {
    constexpr uint32_t kGrowthFactor = 2;
    InternalAllocate(count * kGrowthFactor);
  }

  // Obtain the required number of free descriptor sets.
  std::vector<vk::DescriptorSet> allocated_sets;
  allocated_sets.reserve(count);
  for (size_t i = free_sets_.size() - count; i < free_sets_.size(); ++i) {
    allocated_sets.push_back(free_sets_[i]);
  }
  free_sets_.resize(free_sets_.size() - count);

  auto allocation = ftl::AdoptRef(
      new DescriptorSetAllocation(this, std::move(allocated_sets)));
  ++allocation_count_;

  if (command_buffer) {
    command_buffer->AddUsedResource(allocation);
  }

  return allocation;
}

void DescriptorSetPool::InternalAllocate(uint32_t descriptor_set_count) {
  // Allocate a new pool large enough to allocate the desired number of sets.
  vk::DescriptorPoolCreateInfo pool_info;
  auto counts = descriptor_counts_;
  for (auto& c : counts) {
    c.descriptorCount *= descriptor_set_count;
  }
  pool_info.poolSizeCount = static_cast<uint32_t>(counts.size());
  pool_info.pPoolSizes = counts.data();
  pool_info.maxSets = descriptor_set_count;
  auto pool = ESCHER_CHECKED_VK_RESULT(device_.createDescriptorPool(pool_info));
  pools_.push_back(pool);

  // Allocate the new descriptor sets.
  vk::DescriptorSetAllocateInfo allocate_info;
  allocate_info.descriptorPool = pool;
  allocate_info.descriptorSetCount = descriptor_set_count;
  std::vector<vk::DescriptorSetLayout> layouts(descriptor_set_count, layout_);
  allocate_info.pSetLayouts = layouts.data();
  std::vector<vk::DescriptorSet> new_descriptor_sets(
      ESCHER_CHECKED_VK_RESULT(device_.allocateDescriptorSets(allocate_info)));

  // Add the newly-allocated descriptor sets to the free list.
  free_sets_.reserve(free_sets_.capacity() + descriptor_set_count);
  for (auto s : new_descriptor_sets) {
    free_sets_.push_back(s);
  }
}

void DescriptorSetPool::ReturnDescriptorSets(
    std::vector<vk::DescriptorSet> unused_sets) {
  auto count = --allocation_count_;
  FTL_DCHECK(count >= 0);
  free_sets_.insert(free_sets_.end(), unused_sets.begin(), unused_sets.end());
}

}  // namespace impl
}  // namespace escher
