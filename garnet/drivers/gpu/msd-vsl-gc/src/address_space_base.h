// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_GPU_MSD_VSL_GC_SRC_ADDRESS_SPACE_BASE_H_
#define GARNET_DRIVERS_GPU_MSD_VSL_GC_SRC_ADDRESS_SPACE_BASE_H_

#include <map>
#include <unordered_map>

#include "gpu_mapping.h"

class AddressSpaceBase {
 public:
  using gpu_addr_t = uint32_t;
  static_assert(sizeof(GpuMapping::gpu_addr_t) == sizeof(gpu_addr_t), "size mismatch");

  virtual ~AddressSpaceBase() {}

  virtual bool Insert(gpu_addr_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping,
                      uint64_t page_count) = 0;
  virtual bool Clear(gpu_addr_t addr, uint64_t page_count) = 0;

  bool AddMapping(std::unique_ptr<GpuMapping> gpu_mapping);
  bool RemoveMapping(magma::PlatformBuffer* buffer, gpu_addr_t gpu_addr);

  void ReleaseBuffer(magma::PlatformBuffer* buffer, uint32_t* released_count_out);

 private:
  using map_container_t = std::map<gpu_addr_t, std::shared_ptr<GpuMapping>>;
  // Container of gpu mappings by address
  map_container_t mappings_;
  // Container of references to entries in mappings_ by buffer;
  // useful for cleaning up mappings when connections go away, and when
  // buffers are released.
  std::unordered_multimap<magma::PlatformBuffer*, map_container_t::iterator> mappings_by_buffer_;

  friend class TestAddressSpaceBase;
};

#endif  // GARNET_DRIVERS_GPU_MSD_VSL_GC_SRC_ADDRESS_SPACE_BASE_H_
