// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_GPU_MAPPING_H_
#define SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_GPU_MAPPING_H_

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "magma_util/macros.h"
#include "platform_bus_mapper.h"
#include "region.h"

class MsdArmBuffer;
class MsdArmConnection;

struct BusMappingCompare {
  bool operator()(const std::unique_ptr<magma::PlatformBusMapper::BusMapping>& a,
                  const std::unique_ptr<magma::PlatformBusMapper::BusMapping>& b) const {
    return a->page_offset() < b->page_offset();
  }
};

// A buffer may be mapped into a connection at multiple virtual addresses. The
// connection owns the GpuMapping, so |owner_| is always valid. The buffer
// deletes all the mappings it owns before it's destroyed, so that's why
// |buffer_| is always valid.
class GpuMapping {
 public:
  class Owner {
   public:
    virtual bool RemoveMapping(uint64_t address) = 0;
    virtual bool UpdateCommittedMemory(GpuMapping* mapping) = 0;
  };

  GpuMapping(uint64_t addr, uint64_t page_offset, uint64_t size, uint64_t flags, Owner* owner,
             std::shared_ptr<MsdArmBuffer> buffer);

  ~GpuMapping();

  uint64_t gpu_va() const { return addr_; }
  uint64_t page_offset() const { return page_offset_; }  // In CPU pages.
  uint64_t size() const { return size_; }
  uint64_t flags() const { return flags_; }

  void ReplaceBusMappings(std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping) {
    if (bus_mapping) {
      DASSERT(bus_mapping->page_offset() >= page_offset_);
      DASSERT(bus_mapping->page_offset() + bus_mapping->page_count() <=
              page_offset_ + (size_ / PAGE_SIZE));
      if (magma::kDebug) {
        // Check that the physical addresses of pages haven't changed
        // (e.g. due to being mapped to a new place with the iommu).
        for (auto& mapping : bus_mappings_) {
          for (uint32_t i = 0; i < mapping->page_count(); i++) {
            if (mapping->page_offset() + i < bus_mapping->page_offset())
              continue;
            uint64_t new_offset = mapping->page_offset() + i - bus_mapping->page_offset();
            if (new_offset >= bus_mapping->page_count())
              continue;
            DASSERT(bus_mapping->Get()[new_offset] == mapping->Get()[i]);
          }
        }
      }
    }
    bus_mappings_.clear();
    Region bus_mapping_region;
    if (bus_mapping) {
      bus_mapping_region =
          Region::FromStartAndLength(bus_mapping->page_offset(), bus_mapping->page_count());
      bus_mappings_.insert(std::move(bus_mapping));
    }
    committed_region_in_buffer_ = bus_mapping_region;
  }
  void AddBusMapping(std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping) {
    DASSERT(bus_mapping);
    auto bus_mapping_region =
        Region::FromStartAndLength(bus_mapping->page_offset(), bus_mapping->page_count());
    if (!committed_region_in_buffer_.empty()) {
      DASSERT(bus_mapping_region.IsAdjacentTo(committed_region_in_buffer()));
    }
    bus_mappings_.insert(std::move(bus_mapping));
    committed_region_in_buffer_.Union(bus_mapping_region);
  }

  std::weak_ptr<MsdArmBuffer> buffer() const;
  void Remove() { owner_->RemoveMapping(addr_); }
  bool UpdateCommittedMemory() { return owner_->UpdateCommittedMemory(this); }

  const std::set<std::unique_ptr<magma::PlatformBusMapper::BusMapping>, BusMappingCompare>&
  bus_mappings() {
    return bus_mappings_;
  }

  // Returns committed region in pages relative to the start of the mapping.
  Region committed_region() const {
    return Region::FromStartAndLength(committed_region_in_buffer_.start() - page_offset_,
                                      committed_region_in_buffer_.length());
  }
  Region committed_region_in_buffer() const { return committed_region_in_buffer_; }

 private:
  friend class TestConnection;
  const uint64_t addr_;
  const uint64_t page_offset_;
  // In bytes
  const uint64_t size_;
  const uint64_t flags_;
  // Region in pages relative to the beginning of the buffer. This is stored as an optimization so
  // we don't have to union the regions in bus_mappings_ whenever this is queried.
  Region committed_region_in_buffer_;
  Owner* const owner_;
  std::weak_ptr<MsdArmBuffer> buffer_;
  // Bus mappings must be contiguous and completely cover committed_region_in_buffer_.
  std::set<std::unique_ptr<magma::PlatformBusMapper::BusMapping>, BusMappingCompare> bus_mappings_;
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_GPU_MAPPING_H_
