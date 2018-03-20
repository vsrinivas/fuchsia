// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtt.h"

#include <vector>

#include "address_space.h"
#include "gpu_mapping_cache.h"
#include "magma_util/address_space_allocator.h"
#include "magma_util/macros.h"
#include "magma_util/simple_allocator.h"
#include "platform_buffer.h"
#include "platform_bus_mapper.h"
#include "platform_pci_device.h"

class GttCore : public Gtt {
public:
    GttCore(Owner* owner);

    uint64_t Size() const override { return size_; }

    bool Init(uint64_t gtt_size) override;

    // AddressSpace overrides
    bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) override;
    bool Free(uint64_t addr) override;

    bool Clear(uint64_t addr) override;

    bool GlobalGttInsert(uint64_t addr, magma::PlatformBuffer* buffer,
                         magma::PlatformBusMapper::BusMapping* bus_mapping, uint64_t page_offset,
                         uint64_t page_count, CachingType caching_type) override
    {
        return Insert(addr, bus_mapping, page_offset, page_count, caching_type);
    }
    bool Insert(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping,
                uint64_t page_offset, uint64_t page_count, CachingType caching_type) override;

private:
    uint64_t pte_mmio_offset() { return mmio_->size() / 2; }

    magma::PlatformBuffer* scratch_buffer() { return scratch_.get(); }

    bool MapGttMmio(magma::PlatformPciDevice* platform_device);
    bool InitScratch();
    bool InitPageTables(uint64_t start);
    bool Clear(uint64_t start, uint64_t length);

private:
    Owner* owner_;
    std::unique_ptr<magma::PlatformMmio> mmio_;
    std::unique_ptr<magma::PlatformBuffer> scratch_;
    std::unique_ptr<magma::AddressSpaceAllocator> allocator_;

    // Protect all AddressSpace methods because of access from gpu and core device.
    std::mutex mutex_;

    std::unique_ptr<magma::PlatformBusMapper::BusMapping> scratch_bus_mapping_;
    uint64_t size_;

    friend class TestGtt;
};

static inline gen_pte_t gen_pte_encode(uint64_t bus_addr, bool valid)
{
    gen_pte_t pte = bus_addr | PAGE_RW;
    if (valid)
        pte |= PAGE_PRESENT;

    return pte;
}

GttCore::GttCore(Owner* owner) : Gtt(owner), owner_(owner) {}

bool GttCore::Init(uint64_t gtt_size)
{
    // address space size
    size_ = (gtt_size / sizeof(gen_pte_t)) * PAGE_SIZE;

    DLOG("Gtt::Init gtt_size (for page tables) 0x%lx size (address space) 0x%lx ", gtt_size, size_);

    if (!MapGttMmio(owner_->platform_device()))
        return DRETF(false, "MapGttMmio failed");

    // gtt pagetables are in the 2nd half of bar 0
    if (gtt_size > mmio_->size() / 2)
        return DRETF(false, "mmio space too small for gtt");

    DLOG("mmio_base %p size 0x%lx gtt_size 0x%lx", mmio_->addr(), mmio_->size(), gtt_size);

    if (!InitScratch())
        return DRETF(false, "InitScratch failed");

    if (!InitPageTables(0))
        return DRETF(false, "InitPageTables failed");

    return true;
}

bool GttCore::InitPageTables(uint64_t start)
{
    // leave space for a guard page
    allocator_ = magma::SimpleAllocator::Create(start, size_ - PAGE_SIZE);
    if (!allocator_)
        return DRETF(false, "failed to create allocator");

    if (!Clear(start, size_))
        return DRETF(false, "Clear failed");

    return true;
}

bool GttCore::MapGttMmio(magma::PlatformPciDevice* platform_device)
{
    mmio_ = platform_device->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    if (!mmio_)
        return DRETF(false, "failed to map pci bar 0");

    return true;
}

bool GttCore::InitScratch()
{
    scratch_ = magma::PlatformBuffer::Create(PAGE_SIZE, "gtt-scratch");

    scratch_bus_mapping_ = owner_->GetBusMapper()->MapPageRangeBus(scratch_.get(), 0, 1);
    if (!scratch_bus_mapping_)
        return DRETF(false, "MapPageBus failed");

    return true;
}

bool GttCore::Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out)
{
    DASSERT(allocator_);
    std::lock_guard<std::mutex> lock(mutex_);
    // allocate an extra page on the end to avoid page faults from over fetch
    // see
    // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-skl-vol02a-commandreference-instructions.pdf
    // page 908
    size_t alloc_size = size + PAGE_SIZE;
    return allocator_->Alloc(alloc_size, align_pow2, addr_out);
}

bool GttCore::Free(uint64_t addr)
{
    DASSERT(allocator_);
    std::lock_guard<std::mutex> lock(mutex_);
    return allocator_->Free(addr);
}

bool GttCore::Clear(uint64_t addr)
{
    DASSERT(allocator_);

    std::lock_guard<std::mutex> lock(mutex_);

    size_t length;
    if (!allocator_->GetSize(addr, &length))
        return DRETF(false, "couldn't get size for addr");
    if (!Clear(addr, length))
        return DRETF(false, "clear failed");
    return true;
}

bool GttCore::Clear(uint64_t start, uint64_t length)
{
    DASSERT((start & (PAGE_SIZE - 1)) == 0);
    DASSERT((length & (PAGE_SIZE - 1)) == 0);

    const uint64_t max_entries = Size() >> PAGE_SHIFT;
    uint64_t first_entry = start >> PAGE_SHIFT;
    uint64_t num_entries = length >> PAGE_SHIFT;

    DLOG("first_entry 0x%lx num_entries %ld max_entries %ld", first_entry, num_entries,
         max_entries);

    if (first_entry + num_entries > max_entries)
        return DRETF(false, "exceeded max_entries");

    gen_pte_t pte = gen_pte_encode(scratch_bus_mapping_->Get()[0], false);

    uint64_t pte_offset = pte_mmio_offset() + first_entry * sizeof(gen_pte_t);

    for (unsigned int i = 0; i < num_entries; i++) {
        mmio_->Write64(pte_offset + i * sizeof(gen_pte_t), static_cast<uint64_t>(pte));
    }

    mmio_->PostingRead32(pte_offset + (num_entries - 1) * sizeof(gen_pte_t));

    return true;
}

bool GttCore::Insert(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping,
                     uint64_t page_offset, uint64_t page_count, CachingType caching_type)
{
    DLOG("InsertEntries addr 0x%lx", addr);

    std::lock_guard<std::mutex> lock(mutex_);

    size_t allocated_length;
    if (!allocator_->GetSize(addr, &allocated_length))
        return DRETF(false, "couldn't get allocated length for addr");

    // add an extra page to length to account for the overfetch protection page
    if (page_count * PAGE_SIZE + PAGE_SIZE != allocated_length)
        return DRETF(false, "allocated length (0x%zx) doesn't match length (0x%" PRIx64 ")",
                     allocated_length, page_count * PAGE_SIZE);

    auto& bus_addr_array = bus_mapping->Get();
    if (bus_addr_array.size() != page_count)
        return DRETF(false, "incorrect bus mapping length");

    uint64_t first_entry = addr >> PAGE_SHIFT;
    uint64_t pte_offset = pte_mmio_offset() + first_entry * sizeof(gen_pte_t);

    for (unsigned int i = 0; i < page_count; i++) {
        auto pte = gen_pte_encode(bus_addr_array[i], true);
        mmio_->Write64(pte_offset + i * sizeof(gen_pte_t), static_cast<uint64_t>(pte));
    }

    // insert pte for overfetch protection page
    auto pte = gen_pte_encode(scratch_bus_mapping_->Get()[0], true);
    mmio_->Write64(pte_offset + (page_count) * sizeof(gen_pte_t), static_cast<uint64_t>(pte));

    uint64_t readback = mmio_->PostingRead64(pte_offset + (page_count - 1) * sizeof(gen_pte_t));

    if (magma::kDebug) {
        auto expected = gen_pte_encode(bus_addr_array[page_count - 1], true);
        if (readback != expected) {
            DLOG("Mismatch posting read: 0x%0lx != 0x%0lx", readback, expected);
            DASSERT(false);
        }
    }

    return true;
}

std::unique_ptr<Gtt> Gtt::CreateCore(Owner* owner)
{
    return std::make_unique<GttCore>(owner);
}
