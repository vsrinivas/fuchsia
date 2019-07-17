// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LINUX_PLATFORM_BUS_MAPPER_H
#define LINUX_PLATFORM_BUS_MAPPER_H

#include <vector>

#include "linux_platform_buffer.h"
#include "linux_platform_handle.h"
#include "platform_bus_mapper.h"

namespace magma {

class LinuxPlatformBusMapper : public PlatformBusMapper {
 public:
  LinuxPlatformBusMapper(std::shared_ptr<LinuxPlatformHandle> bus_transaction_initiator)
      : bus_transaction_initiator_(std::move(bus_transaction_initiator)) {}

  std::unique_ptr<BusMapping> MapPageRangeBus(PlatformBuffer* buffer, uint32_t start_page_index,
                                              uint32_t page_count) override;

  std::unique_ptr<PlatformBuffer> CreateContiguousBuffer(size_t size, uint32_t alignment_log2,
                                                         const char* name) override {
    return DRETP(nullptr, "CreateContiguousBuffer not supported");
  }

  class BusMapping : public PlatformBusMapper::BusMapping {
   public:
    BusMapping(uint64_t page_offset, std::vector<uint64_t> page_addr, LinuxPlatformHandle dma_buf,
               uint64_t token)
        : page_offset_(page_offset),
          page_addr_(std::move(page_addr)),
          dma_buf_(dma_buf.release()),
          token_(token) {}

    uint64_t page_offset() override { return page_offset_; }
    uint64_t page_count() override { return page_addr_.size(); }
    uint64_t token() { return token_; }

    std::vector<uint64_t>& Get() override { return page_addr_; }

   private:
    uint64_t page_offset_;
    std::vector<uint64_t> page_addr_;
    LinuxPlatformHandle dma_buf_;
    uint64_t token_;
  };

 private:
  std::shared_ptr<LinuxPlatformHandle> bus_transaction_initiator_;
};

}  // namespace magma

#endif  // LINUX_PLATFORM_BUS_MAPPER_H
