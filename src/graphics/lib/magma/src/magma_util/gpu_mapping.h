// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_GPU_MAPPING_H
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_GPU_MAPPING_H

#include <platform_bus_mapper.h>

#include <memory>
#include <vector>

#include <magma_util/macros.h>

#include "accessor.h"

namespace magma {

// GpuMappingView exposes a non-mutable interface to a GpuMapping.
template <typename Buffer>
class GpuMappingView {
 public:
  using BufferType = Buffer;

  GpuMappingView(std::shared_ptr<Buffer> buffer, uint64_t gpu_addr, uint64_t offset,
                 uint64_t length)
      : buffer_(std::move(buffer)), gpu_addr_(gpu_addr), offset_(offset), length_(length) {}

  uint64_t gpu_addr() const { return gpu_addr_; }

  uint64_t offset() const { return offset_; }

  // Length of a GpuMapping is mutable; this method is racy if called from a thread other
  // than the connection thread.
  uint64_t length() const { return length_; }

  uint64_t BufferId() const { return BufferAccessor<Buffer>::platform_buffer(buffer_.get())->id(); }

  uint64_t BufferSize() const {
    return BufferAccessor<Buffer>::platform_buffer(buffer_.get())->size();
  }

  bool Copy(std::vector<uint32_t>* buffer_out) const;

 protected:
  ~GpuMappingView() = default;

  std::shared_ptr<Buffer> buffer_;
  const uint64_t gpu_addr_;
  const uint64_t offset_;
  uint64_t length_;
};

template <typename GpuMapping>
class AddressSpace;

// GpuMapping is created by a connection thread, and mutated only by that connection thread.
// However, shared references to GpuMapping may be taken by command buffers, keeping them alive
// while the mappings are in flight.
// Therefore, GpuMappings can be destroyed from the device thread, if the connection has removed
// all its references.
// Mutation of the page tables in an AddressSpace is therefore thread locked.
template <typename Buffer>
class GpuMapping : public GpuMappingView<Buffer> {
 public:
  GpuMapping(std::shared_ptr<AddressSpace<GpuMapping>> address_space,
             std::shared_ptr<Buffer> buffer, uint64_t offset, uint64_t length, uint64_t gpu_addr,
             std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping)
      : GpuMappingView<Buffer>(std::move(buffer), gpu_addr, offset, length),
        address_space_(address_space) {
    bus_mappings_.emplace_back(std::move(bus_mapping));
  }

  ~GpuMapping() { Release(nullptr); }

  Buffer* buffer() { return GpuMappingView<Buffer>::buffer_.get(); }

  std::weak_ptr<AddressSpace<GpuMapping>> address_space() const { return address_space_; }

  // Add the given |bus_mapping|.
  // Note that length() changes as a result.
  void Grow(std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping) {
    GpuMappingView<Buffer>::length_ += bus_mapping->page_count() * magma::page_size();
    bus_mappings_.emplace_back(std::move(bus_mapping));
  }

  // Releases the gpu mapping, returns all bus mappings in |bus_mappings_out|.
  // Called by the device thread (via destructor), or connection thread.
  bool Release(
      std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>* bus_mappings_out);

 private:
  std::weak_ptr<AddressSpace<GpuMapping>> address_space_;
  std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mappings_;
};

template <typename Buffer>
bool GpuMappingView<Buffer>::Copy(std::vector<uint32_t>* buffer_out) const {
  auto platform_buffer = BufferAccessor<Buffer>::platform_buffer(buffer_.get());

  void* data;
  if (!platform_buffer->MapCpu(&data))
    return DRETF(false, "couldn't map buffer");

  buffer_out->resize(platform_buffer->size());
  std::memcpy(buffer_out->data(), data, buffer_out->size());

  platform_buffer->UnmapCpu();
  return true;
}

template <typename Buffer>
bool GpuMapping<Buffer>::Release(
    std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>>* bus_mappings_out) {
  auto address_space = address_space_.lock();

  bool success = true;
  if (address_space) {
    uint64_t addr = GpuMappingView<Buffer>::gpu_addr();
    DASSERT(bus_mappings_.size());
    if (!bus_mappings_[0]) {
      if (!address_space->Clear(addr, nullptr))
        success = false;
    } else {
      for (auto& bus_mapping : bus_mappings_) {
        DASSERT(bus_mapping);
        if (!address_space->Clear(addr, bus_mapping.get()))
          success = false;
        addr += bus_mapping->page_count() * magma::page_size();
      }
    }

    if (!address_space->Free(GpuMappingView<Buffer>::gpu_addr()))
      success = false;
  }

  GpuMappingView<Buffer>::buffer_.reset();
  address_space_.reset();
  GpuMappingView<Buffer>::length_ = 0;
  if (bus_mappings_out) {
    *bus_mappings_out = std::move(bus_mappings_);
  }
  bus_mappings_.clear();
  return success;
}

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_GPU_MAPPING_H
