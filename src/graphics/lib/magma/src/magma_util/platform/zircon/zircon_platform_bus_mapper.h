// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_BUS_MAPPER_H
#define ZIRCON_PLATFORM_BUS_MAPPER_H

#include <lib/zx/pmt.h>

#include <vector>

#include "platform_bus_mapper.h"
#include "zircon_platform_buffer.h"
#include "zircon_platform_handle.h"

namespace magma {

class ZirconPlatformBusMapper : public PlatformBusMapper {
 public:
  ZirconPlatformBusMapper(std::shared_ptr<ZirconPlatformHandle> bus_transaction_initiator)
      : bus_transaction_initiator_(std::move(bus_transaction_initiator)) {}

  std::unique_ptr<BusMapping> MapPageRangeBus(PlatformBuffer* buffer, uint64_t start_page_index,
                                              uint64_t page_count) override;
  std::unique_ptr<PlatformBuffer> CreateContiguousBuffer(size_t size, uint32_t alignment_log2,
                                                         const char* name) override;

  class BusMapping : public PlatformBusMapper::BusMapping {
   public:
    BusMapping(uint64_t page_offset, std::vector<uint64_t> page_addr, zx::pmt pmt)
        : page_offset_(page_offset), page_addr_(std::move(page_addr)), pmt_(std::move(pmt)) {}
    ~BusMapping();

    uint64_t page_offset() override { return page_offset_; }
    uint64_t page_count() override { return page_addr_.size(); }

    std::vector<uint64_t>& Get() override { return page_addr_; }

   private:
    uint64_t page_offset_;
    std::vector<uint64_t> page_addr_;
    zx::pmt pmt_;
  };

 private:
  std::shared_ptr<ZirconPlatformHandle> bus_transaction_initiator_;
};

}  // namespace magma

#endif  // ZIRCON_PLATFORM_BUS_MAPPER_H
