// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTT_H
#define GTT_H

#include <memory>
#include "address_space.h"
#include "platform_pci_device.h"

class Gtt : public AddressSpace {
public:
    class Owner {
    public:
        virtual magma::PlatformPciDevice* platform_device() = 0;
    };

    Gtt() : AddressSpace(ADDRESS_SPACE_GGTT, nullptr) {}

    virtual bool Init(uint64_t gtt_size) = 0;

    // Takes ownership of |buffer_handle|.
    virtual bool Insert(uint64_t addr, uint32_t buffer_handle, uint64_t offset, uint64_t length,
                        CachingType caching_type) = 0;

    bool Insert(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t offset, uint64_t length,
                CachingType caching_type) override
    {
        // Insert expects that pages are pinned.
        // Currently, gtt core does pin/unpin in order to import the handle as PlatformBuffer.
        // Unpin first so we can pin after without running up the pin count.
        // TODO(MA-404) - remove this.
        buffer->UnpinPages(offset / PAGE_SIZE, length / PAGE_SIZE);

        uint32_t handle;
        if (!buffer->duplicate_handle(&handle))
            return DRETF(false, "failed to duplicate handle");

        bool result = Insert(addr, handle, offset, length, caching_type);

        // Repin the pages.
        buffer->PinPages(offset / PAGE_SIZE, length / PAGE_SIZE);
        return result;
    }

    static std::unique_ptr<Gtt> CreateShim(Owner* owner);
    static std::unique_ptr<Gtt> CreateCore(Owner* owner);

    friend class TestGtt;
};

#endif // GTT_H
