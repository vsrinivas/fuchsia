// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_ACCESSOR_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_ACCESSOR_H_

#include <platform_buffer.h>
#include <platform_bus_mapper.h>

namespace magma {

// Utility classes such as AddressSpace and GpuMapping operate on Buffers,
// but the type of buffer class used by particular drivers may vary.
// The utilities are templated and use the Accessor classes defined here
// to clarify the interfaces required.
template <typename Buffer>
class BufferAccessor {
 public:
  static magma::PlatformBuffer* platform_buffer(Buffer* buffer) {
    return buffer->platform_buffer();
  }
};

template <>
class BufferAccessor<magma::PlatformBuffer> {
 public:
  static magma::PlatformBuffer* platform_buffer(magma::PlatformBuffer* buffer) { return buffer; }
};

template <typename GpuMapping>
class AddressSpace;

template <typename GpuMapping>
class GpuMappingAccessor {
 public:
  // GpuMapping must declare the type of its containing buffer as BufferType
  using Buffer = typename GpuMapping::BufferType;

  // Create a GpuMapping.
  static std::unique_ptr<GpuMapping> Create(
      std::shared_ptr<AddressSpace<GpuMapping>> address_space, std::shared_ptr<Buffer> buffer,
      uint64_t offset, uint64_t length, uint64_t gpu_addr,
      std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping) {
    return std::make_unique<GpuMapping>(std::move(address_space), std::move(buffer), offset, length,
                                        gpu_addr, std::move(bus_mapping));
  }

  // Access elements of a GpuMapping.
  static Buffer* buffer(GpuMapping* mapping) { return mapping->buffer(); }
  static uint64_t gpu_addr(GpuMapping* mapping) { return mapping->gpu_addr(); }
  static uint64_t offset(GpuMapping* mapping) { return mapping->offset(); }
  static uint64_t length(GpuMapping* mapping) { return mapping->length(); }
};

template <typename Context, typename AddressSpace>
class ContextAccessor {
 public:
  static std::shared_ptr<AddressSpace> exec_address_space(Context* context) {
    return context->exec_address_space();
  }
};

}  // namespace magma

#endif  // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_ACCESSOR_H_
