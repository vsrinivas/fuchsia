// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_MAPPING_H
#define GPU_MAPPING_H

#include <memory>

#include "msd_vsl_buffer.h"
#include "platform_bus_mapper.h"

class AddressSpaceBase;

class GpuMapping {
 public:
  using gpu_addr_t = uint32_t;

  GpuMapping(std::weak_ptr<AddressSpaceBase> address_space, std::shared_ptr<MsdVslBuffer> buffer,
             std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping, gpu_addr_t gpu_addr)
      : address_space_(address_space),
        buffer_(std::move(buffer)),
        bus_mapping_(std::move(bus_mapping)),
        gpu_addr_(gpu_addr) {}

  ~GpuMapping();

  std::weak_ptr<AddressSpaceBase> address_space() { return address_space_; }

  MsdVslBuffer* buffer() { return buffer_.get(); }

  magma::PlatformBusMapper::BusMapping* bus_mapping() { return bus_mapping_.get(); }

  uint64_t page_offset() { return bus_mapping_->page_offset(); }

  uint64_t page_count() { return bus_mapping_->page_count(); }

  gpu_addr_t gpu_addr() { return gpu_addr_; }

 private:
  std::weak_ptr<AddressSpaceBase> address_space_;
  std::shared_ptr<MsdVslBuffer> buffer_;
  std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping_;
  gpu_addr_t gpu_addr_{};
};

#endif  // GPU_MAPPING_H
