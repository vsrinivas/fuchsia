// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SIMPLE_ALLOCATOR_H
#define SIMPLE_ALLOCATOR_H

#include "address_space_allocator.h"
#include <list>

class SimpleAllocator : public AddressSpaceAllocator {
public:
    static std::unique_ptr<SimpleAllocator> Create(uint64_t base, size_t size);

    ~SimpleAllocator();

    bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) override;
    bool Free(uint64_t addr) override;
    bool GetSize(uint64_t addr, size_t* size_out) override;

private:
    SimpleAllocator(uint64_t base, size_t size);

    struct Region {
        Region(uint64_t base, size_t size);
        uint64_t base;
        size_t size;
    };

    bool CheckGap(SimpleAllocator::Region* prev, SimpleAllocator::Region* next, uint64_t align,
                  size_t size, uint64_t* addr_out, bool* continue_search_out);

    DISALLOW_COPY_AND_ASSIGN(SimpleAllocator);

private:
    std::list<SimpleAllocator::Region>::iterator FindRegion(uint64_t addr);

    std::list<SimpleAllocator::Region> regions_;
};

#endif // SIMPLE_ALLOCATOR_H
