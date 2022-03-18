// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEV_MEM_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEV_MEM_H_

#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <algorithm>
#include <set>

#include "src/virtualization/bin/vmm/guest.h"

class DevMem {
 public:
  struct Range {
    zx_gpaddr_t addr;
    size_t size;

    bool operator<(const Range& r) const { return addr + size <= r.addr; }

    bool contains(const Range& r) const { return !(r < *this) && !(*this < r); }
  };
  using RangeSet = std::set<Range>;

  [[nodiscard]] bool AddRange(zx_gpaddr_t addr, size_t size) {
    if (finalized_) {
      FX_LOGS(ERROR) << "Cannot add device memory ranges after finalizing the set";
      return false;
    }

    if (size == 0) {
      FX_LOGS(WARNING) << "Cannot add zero length ranges";
      return false;
    }

    return ranges_.emplace(Range{addr, size}).second;
  }

  bool HasGuestMemoryOverlap(const std::vector<GuestMemoryRegion>& guest_memory_regions) const {
    zx_gpaddr_t addr = 0;
    size_t size = 0;
    auto yield = [&addr, &size](zx_gpaddr_t addr_in, size_t size_in) {
      addr = addr_in;
      size = size_in;
    };

    // If yielding the inverse range does not exactly match a given guest memory region, it means
    // that there is at least one region of device memory that intersected the provided region
    // of guest memory.
    for (const GuestMemoryRegion& guest_mem : guest_memory_regions) {
      YieldInverseRange(guest_mem.base, guest_mem.size, yield);
      if (addr != guest_mem.base || size != guest_mem.size) {
        FX_LOGS(ERROR) << "Guest memory range " << guest_mem.base << " - "
                       << guest_mem.base + guest_mem.size << " overlaps with device memory";
        return true;
      }
    }

    return false;
  }

  // Called to prevent adding additional device memory ranges. This allows the Guest to validate
  // that there is no overlap between guest memory and device memory before starting the VM.
  void Finalize() { finalized_ = true; }

  const RangeSet::const_iterator begin() const { return ranges_.begin(); }
  const RangeSet::const_iterator end() const { return ranges_.end(); }

  // Generates, by calling the provided functor, all Range's that are in the
  // provided range, that do not overlap with any internal ranges. This means
  // the generated set is precisely the inverse of our contained ranges, unioned
  // with the provided range.
  template <typename F>
  void YieldInverseRange(zx_gpaddr_t base, size_t size, F yield) const {
    zx_gpaddr_t prev = base;
    for (const auto& range : ranges_) {
      zx_gpaddr_t next_top = std::min(range.addr, base + size);
      if (next_top > prev) {
        yield(prev, next_top - prev);
      }
      prev = range.addr + range.size;
    }
    zx_gpaddr_t next_top = std::max(prev, base + size);
    if (next_top > prev) {
      yield(prev, next_top - prev);
    }
  }

 private:
  RangeSet ranges_;
  bool finalized_ = false;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEV_MEM_H_
