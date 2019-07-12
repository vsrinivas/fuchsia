// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_space_base.h"

bool AddressSpaceBase::AddMapping(std::unique_ptr<GpuMapping> gpu_mapping) {
  auto iter = mappings_.upper_bound(gpu_mapping->gpu_addr());
  if (iter != mappings_.end() &&
      (gpu_mapping->gpu_addr() + gpu_mapping->page_count() * PAGE_SIZE > iter->second->gpu_addr()))
    return DRETF(false, "Mapping overlaps existing mapping");
  // Find the mapping with the highest VA that's <= this.
  if (iter != mappings_.begin()) {
    --iter;
    // Check if the previous mapping overlaps this.
    if (iter->second->gpu_addr() + iter->second->page_count() * PAGE_SIZE > gpu_mapping->gpu_addr())
      return DRETF(false, "Mapping overlaps existing mapping");
  }

  if (!Insert(gpu_mapping->gpu_addr(), gpu_mapping->bus_mapping(), gpu_mapping->page_count()))
    return DRETF(false, "failed to insert mapping");

  auto platform_buffer = gpu_mapping->buffer()->platform_buffer();

  std::pair<map_container_t::iterator, bool> result =
      mappings_.insert({gpu_mapping->gpu_addr(), std::move(gpu_mapping)});
  DASSERT(result.second);

  mappings_by_buffer_.insert({platform_buffer, result.first});

  return true;
}

bool AddressSpaceBase::RemoveMapping(magma::PlatformBuffer* buffer, gpu_addr_t gpu_addr) {
  auto range = mappings_by_buffer_.equal_range(buffer);
  for (auto iter = range.first; iter != range.second; iter++) {
    std::shared_ptr<GpuMapping> gpu_mapping = iter->second->second;
    if (gpu_mapping->gpu_addr() == gpu_addr) {
      mappings_.erase(iter->second);
      mappings_by_buffer_.erase(iter);
      return true;
    }
  }
  return DRETF(false, "failed to remove mapping");
}

void AddressSpaceBase::ReleaseBuffer(magma::PlatformBuffer* buffer, uint32_t* released_count_out) {
  *released_count_out = 0;
  auto range = mappings_by_buffer_.equal_range(buffer);
  for (auto iter = range.first; iter != range.second;) {
    mappings_.erase(iter->second);
    iter = mappings_by_buffer_.erase(iter);
    *released_count_out += 1;
  }
}
