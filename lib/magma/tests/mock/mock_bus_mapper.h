// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOCK_BUS_MAPPER_H
#define MOCK_BUS_MAPPER_H

#include "platform_bus_mapper.h"
#include <vector>

class MockBusMapper : public magma::PlatformBusMapper {
public:
    MockBusMapper(uint64_t start_addr = 0x0000100000000000) : start_addr_(start_addr) {}

    class BusMapping : public magma::PlatformBusMapper::BusMapping {
    public:
        BusMapping(uint64_t page_offset, uint64_t page_count)
            : page_offset_(page_offset), page_addr_(page_count)
        {
        }

        uint64_t page_offset() override { return page_offset_; }
        uint64_t page_count() override { return page_addr_.size(); }

        std::vector<uint64_t>& Get() override { return page_addr_; }

    private:
        uint64_t page_offset_;
        std::vector<uint64_t> page_addr_;
        friend class MockBusMapper;
    };

    std::unique_ptr<magma::PlatformBusMapper::BusMapping>
    MapPageRangeBus(magma::PlatformBuffer* buffer, uint32_t start_page_index,
                    uint32_t page_count) override
    {
        auto mapping = std::make_unique<BusMapping>(start_page_index, page_count);
        for (auto& addr : mapping->page_addr_) {
            addr = start_addr_;
            start_addr_ += PAGE_SIZE;
        }
        return mapping;
    }

private:
    uint64_t start_addr_;
};

#endif // MOCK_BUS_MAPPER_H
