// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOCK_BUS_MAPPER_H
#define MOCK_BUS_MAPPER_H

#include <vector>

#include "platform_bus_mapper.h"

class MockBusMapper;
class MockConsistentBusMapper;

class MockBusMapping : public magma::PlatformBusMapper::BusMapping {
 public:
  MockBusMapping(uint64_t page_offset, uint64_t page_count)
      : page_offset_(page_offset), page_addr_(page_count) {}

  uint64_t page_offset() override { return page_offset_; }
  uint64_t page_count() override { return page_addr_.size(); }

  std::vector<uint64_t>& Get() override { return page_addr_; }

 private:
  uint64_t page_offset_;
  std::vector<uint64_t> page_addr_;
  friend class MockBusMapper;
  friend class MockConsistentBusMapper;
};

class MockBusMapper : public magma::PlatformBusMapper {
 public:
  MockBusMapper(uint64_t start_addr = 0x0000100000000000) : start_addr_(start_addr) {}

  std::unique_ptr<magma::PlatformBusMapper::BusMapping> MapPageRangeBus(
      magma::PlatformBuffer* buffer, uint64_t start_page_index, uint64_t page_count) override {
    // Prevent mapping unreasonably large numbers of pages.
    if (page_count > (1ul << 33) / PAGE_SIZE)
      return nullptr;
    auto mapping = std::make_unique<MockBusMapping>(start_page_index, page_count);
    for (auto& addr : mapping->page_addr_) {
      addr = start_addr_;
      start_addr_ += PAGE_SIZE;
    }
    return mapping;
  }

  std::unique_ptr<magma::PlatformBuffer> CreateContiguousBuffer(size_t size,
                                                                uint32_t alignment_log2,
                                                                const char* name) override {
    // This mapper maps every buffer contiguously.
    return magma::PlatformBuffer::Create(size, name);
  }

 private:
  uint64_t start_addr_;
};

// A Mock Bus mapper that tries to always map the same location in a buffer to
// the same address.
class MockConsistentBusMapper : public magma::PlatformBusMapper {
 public:
  MockConsistentBusMapper() {}

  std::unique_ptr<magma::PlatformBusMapper::BusMapping> MapPageRangeBus(
      magma::PlatformBuffer* buffer, uint64_t start_page_index, uint64_t page_count) override {
    // Prevent mapping unreasonably large numbers of pages.
    if (page_count > (1ul << 33) / PAGE_SIZE)
      return nullptr;
    auto mapping = std::make_unique<MockBusMapping>(start_page_index, page_count);
    for (uint64_t i = 0; i < page_count; ++i) {
      mapping->page_addr_[i] = (buffer->id() << 24) + ((start_page_index + i) * PAGE_SIZE);
    }
    return mapping;
  }

  std::unique_ptr<magma::PlatformBuffer> CreateContiguousBuffer(size_t size,
                                                                uint32_t alignment_log2,
                                                                const char* name) override {
    // This mapper maps every buffer contiguously.
    return magma::PlatformBuffer::Create(size, name);
  }
};

#endif  // MOCK_BUS_MAPPER_H
