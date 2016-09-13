// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_SPACE_H
#define ADDRESS_SPACE_H

#include "magma_util/platform/platform_buffer.h"
#include "pagetable.h"

// Base class for various address spaces.
class AddressSpace {
public:
    AddressSpace(AddressSpaceId id) : id_(id) {}

    virtual ~AddressSpace() {}

    AddressSpaceId id() { return id_; }

    virtual uint64_t Size() const = 0;

    // Allocates space and returns an address to the start of the allocation.
    virtual bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) = 0;

    // Releases the allocation at the given address.
    virtual bool Free(uint64_t addr) = 0;

    // Clears the page table entries for the allocation at the given address.
    virtual bool Clear(uint64_t addr) = 0;

    // Inserts the pages for the given buffer into page table entries for the allocation at the
    // given address.
    virtual bool Insert(uint64_t addr, magma::PlatformBuffer* buffer, CachingType caching_type) = 0;

private:
    AddressSpaceId id_;
};

#endif // ADDRESS_SPACE_H
