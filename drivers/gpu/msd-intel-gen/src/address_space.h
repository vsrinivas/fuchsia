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

#ifndef VM_H
#define VM_H

#include "magma_util/platform_buffer.h"
#include "pagetable.h"

// Base class for various address spaces.
class AddressSpace {
public:
    AddressSpace() {}

    virtual ~AddressSpace() {}

    // Allocates space and returns an address to the start of the allocation.
    virtual bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) = 0;

    // Releases the allocation at the given address.
    virtual bool Free(uint64_t addr) = 0;

    // Clears the page table entries for the allocation at the given address.
    virtual bool Clear(uint64_t addr) = 0;

    // Inserts the pages for the given buffer into page table entries for the allocation at the
    // given address.
    virtual bool Insert(uint64_t addr, magma::PlatformBuffer* buffer, CachingType caching_type) = 0;
};

#endif // VM_H
