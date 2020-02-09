// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_GPU_MAPPING_H_
#define SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_GPU_MAPPING_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "magma_util/macros.h"
#include "platform_bus_mapper.h"

class MsdArmBuffer;
class MsdArmConnection;

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
  uint64_t page_offset() const { return page_offset_; }
  uint64_t size() const { return size_; }
  uint64_t flags() const { return flags_; }

  uint64_t pinned_page_count() const { return pinned_page_count_; }
  void shrink_pinned_pages(uint64_t pages_removed,
                           std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping) {
    DASSERT(pinned_page_count_ >= pages_removed);
    pinned_page_count_ -= pages_removed;
    if (bus_mapping) {
      DASSERT(pinned_page_count_ == bus_mapping->page_count());
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

      bus_mappings_.clear();
      bus_mappings_.push_back(std::move(bus_mapping));
    }
  }
  void grow_pinned_pages(std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping) {
    pinned_page_count_ += bus_mapping->page_count();
    bus_mappings_.push_back(std::move(bus_mapping));
  }

  std::weak_ptr<MsdArmBuffer> buffer() const;
  void Remove() { owner_->RemoveMapping(addr_); }
  bool UpdateCommittedMemory() { return owner_->UpdateCommittedMemory(this); }

  const std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>& bus_mappings() {
    return bus_mappings_;
  }

 private:
  friend class TestConnection;
  const uint64_t addr_;
  const uint64_t page_offset_;
  const uint64_t size_;
  const uint64_t flags_;
  Owner* const owner_;
  uint64_t pinned_page_count_ = 0;
  std::weak_ptr<MsdArmBuffer> buffer_;
  std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mappings_;
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_GPU_MAPPING_H_
