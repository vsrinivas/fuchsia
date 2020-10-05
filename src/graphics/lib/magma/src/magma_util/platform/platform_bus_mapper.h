// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_BUS_MAPPER_H
#define PLATFORM_BUS_MAPPER_H

#include <memory>

#include "platform_buffer.h"
#include "platform_handle.h"

namespace magma {

class PlatformBusMapper {
 public:
  class BusMapping {
   public:
    virtual ~BusMapping() = default;
    virtual uint64_t page_offset() = 0;
    virtual uint64_t page_count() = 0;
    virtual std::vector<uint64_t>& Get() = 0;
  };

  virtual ~PlatformBusMapper() = default;

  virtual std::unique_ptr<BusMapping> MapPageRangeBus(PlatformBuffer* buffer,
                                                      uint64_t start_page_index,
                                                      uint64_t page_count) = 0;

  // Create a VMO that this bus mapper can map into a contiguous range of pages.
  virtual std::unique_ptr<PlatformBuffer> CreateContiguousBuffer(size_t size,
                                                                 uint32_t alignment_log2,
                                                                 const char* name) = 0;

  static std::unique_ptr<PlatformBusMapper> Create(
      std::shared_ptr<PlatformHandle> bus_transaction_initiator);
};

}  // namespace magma

#endif  // PLATFORM_BUS_MAPPER_H
