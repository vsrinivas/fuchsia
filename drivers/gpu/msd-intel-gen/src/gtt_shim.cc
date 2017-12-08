// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtt.h"
#include "msd_intel_pci_device.h"

class GttShim : public Gtt {
public:
    GttShim(Owner* owner) : owner_(owner) {}

    uint64_t Size() const override { return pci_device()->device()->gtt()->Size(); }

    bool Init(uint64_t gtt_size) override { return pci_device()->device()->gtt()->Init(gtt_size); }

    // AddressSpace overrides
    bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) override
    {
        return pci_device()->device()->gtt()->Alloc(size, align_pow2, addr_out);
    }
    bool Free(uint64_t addr) override { return pci_device()->device()->gtt()->Free(addr); }

    bool Clear(uint64_t addr) override { return pci_device()->device()->gtt()->Clear(addr); }
    bool Insert(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t offset, uint64_t length,
                CachingType caching_type) override
    {
        return pci_device()->device()->gtt()->Insert(addr, buffer, offset, length, caching_type);
    }

private:
    MsdIntelPciDevice* pci_device() const
    {
        return static_cast<MsdIntelPciDevice*>(owner_->platform_device());
    }

    Owner* owner_;
};

std::unique_ptr<Gtt> Gtt::CreateShim(Owner* owner) { return std::make_unique<GttShim>(owner); }
