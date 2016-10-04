// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtt.h"
#include "magma_util/simple_allocator.h"
#include "registers.h"
#include <vector>

Gtt::Gtt(Gtt::Owner* owner) : AddressSpace(ADDRESS_SPACE_GTT), owner_(owner) { DASSERT(owner_); }

Gtt::~Gtt() { DLOG("Gtt dtor"); }

bool Gtt::Init(uint64_t gtt_size, magma::PlatformDevice* platform_device)
{
    // address space size
    size_ = (gtt_size / sizeof(gen_pte_t)) * PAGE_SIZE;

    DLOG("Gtt::Init gtt_size (for page tables) 0x%llx size (address space) 0x%llx ", gtt_size, size_);

    InitPrivatePat();

    if (!MapGttMmio(platform_device))
        return DRETF(false, "MapGttMmio failed");

    // gtt pagetables are in the 2nd half of bar 0
    if (gtt_size > mmio_->size() / 2)
        return DRETF(false, "mmio space too small for gtt");

    DLOG("mmio_base %p size 0x%llx gtt_size 0x%llx", mmio_->addr(), mmio_->size(), gtt_size);

    if (!InitScratch())
        return DRETF(false, "InitScratch failed");

    if (!InitPageTables(0))
        return DRETF(false, "InitPageTables failed");

    return true;
}

bool Gtt::InitPageTables(uint64_t start)
{
    // leave space for a guard page
    allocator_ = SimpleAllocator::Create(start, size_ - PAGE_SIZE);
    if (!allocator_)
        return DRETF(false, "failed to create allocator");

    if (!Clear(start, size_))
        return DRETF(false, "Clear failed");

    return true;
}

bool Gtt::MapGttMmio(magma::PlatformDevice* platform_device)
{
    mmio_ = platform_device->CpuMapPciMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
    if (!mmio_)
        return DRETF(false, "failed to map pci bar 0");

    return true;
}

unsigned int gen_ppat_index(CachingType caching_type)
{
    switch (caching_type) {
    case CACHING_NONE:
        return 3;
    case CACHING_WRITE_THROUGH:
        return 2;
    case CACHING_LLC:
        return 4;
    }
}

// Initialize the private page attribute registers, used to define the meaning
// of the pat bits in the page table entries.
void Gtt::InitPrivatePat()
{
    // While command buffers are executed with GGTT, only index 0 is used and so for the 
    // framebuffer' sake we mark everything as uncacheable.
    // TODO(MA-71) configure the full set of PPAT indices.
    uint64_t pat =
        registers::PatIndex::ppat(0, registers::PatIndex::kLruAgeFromUncore,
                                  registers::PatIndex::kLlc, registers::PatIndex::kUncacheable);
    registers::PatIndex::write(reg_io(), pat);
}

bool Gtt::InitScratch()
{
    scratch_ = magma::PlatformBuffer::Create(PAGE_SIZE);

    if (!scratch_->PinPages())
        return DRETF(false, "PinPages failed");

    if (!scratch_->MapPageBus(0, &scratch_gpu_addr_))
        return DRETF(false, "MapPageBus failed");

#if MAGMA_DEBUG
    {
        void* vaddr;
        if (!scratch_->MapPageCpu(0, &vaddr)) {
            DLOG("MapPageCpu failed");
        } else {
            auto pixel = reinterpret_cast<uint16_t*>(vaddr);
            for (int i = 0; i < 2048; i++)
                pixel[i] = 0xf81f;
            scratch_->UnmapPageCpu(0);
        }
    }
#endif

    return true;
}

bool Gtt::Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out)
{
    DASSERT(allocator_);
    return allocator_->Alloc(size, align_pow2, addr_out);
}

bool Gtt::Free(uint64_t addr)
{
    DASSERT(allocator_);
    return allocator_->Free(addr);
}

bool Gtt::Clear(uint64_t addr)
{
    DASSERT(allocator_);
    size_t length;
    if (!allocator_->GetSize(addr, &length))
        return DRETF(false, "couldn't get size for addr");
    if (!Clear(addr, length))
        return DRETF(false, "clear failed");
    return true;
}

bool Gtt::Clear(uint64_t start, uint64_t length)
{
    DASSERT((start & (PAGE_SIZE - 1)) == 0);
    DASSERT((length & (PAGE_SIZE - 1)) == 0);

    const uint64_t max_entries = Size() >> PAGE_SHIFT;
    uint64_t first_entry = start >> PAGE_SHIFT;
    uint64_t num_entries = length >> PAGE_SHIFT;

    DLOG("first_entry 0x%llx num_entries %lld max_entries %lld", first_entry, num_entries,
         max_entries);

    if (first_entry + num_entries > max_entries)
        return DRETF(false, "exceeded max_entries");

    gen_pte_t pte = gen_pte_encode(scratch_gpu_addr_, CACHING_LLC, false);

    uint64_t pte_offset = pte_mmio_offset() + first_entry * sizeof(gen_pte_t);

    for (unsigned int i = 0; i < num_entries; i++) {
        mmio_->Write64(static_cast<uint64_t>(pte), pte_offset + i * sizeof(gen_pte_t));
    }

    mmio_->PostingRead32(pte_offset + (num_entries - 1) * sizeof(gen_pte_t));

    return true;
}

static inline void unmap(std::vector<uint64_t>& array, magma::PlatformBuffer* buffer)
{
    for (auto addr : array) {
        if (!buffer->UnmapPageBus(addr))
            DLOG("BusUnmap failed");
    }
}

bool Gtt::Insert(uint64_t addr, magma::PlatformBuffer* buffer, CachingType caching_type)
{
    DLOG("InsertEntries addr 0x%llx", addr);

    uint32_t num_pages;

    if (!buffer->PinnedPageCount(&num_pages))
        return DRETF(false, "PinnedPageCount failed");

    if (num_pages == 0)
        return DRETF(false, "num_pages is 0");

    size_t length;
    if (!allocator_->GetSize(addr, &length))
        return DRETF(false, "couldn't get size for addr");

    if (length != num_pages * PAGE_SIZE)
        return DRETF(false, "allocated length doesn't match buffer size");

    uint64_t first_entry = addr >> PAGE_SHIFT;
    uint64_t pte_offset = pte_mmio_offset() + first_entry * sizeof(gen_pte_t);

    std::vector<uint64_t> bus_addr_array;
    bus_addr_array.reserve(num_pages);

    for (unsigned int i = 0; i < num_pages; i++) {
        uint64_t bus_addr;
        if (!buffer->MapPageBus(i, &bus_addr)) {
            unmap(bus_addr_array, buffer);
            return DRETF(false, "failed obtaining page bus addresses");
        }
        bus_addr_array.push_back(bus_addr);
    }

    for (unsigned int i = 0; i < num_pages; i++) {
        auto pte = gen_pte_encode(bus_addr_array[i], caching_type, true);
        mmio_->Write64(static_cast<uint64_t>(pte), pte_offset + i * sizeof(gen_pte_t));
    }

    uint64_t readback = mmio_->PostingRead64(pte_offset + (num_pages - 1) * sizeof(gen_pte_t));
#if MAGMA_DEBUG
    auto expected = gen_pte_encode(bus_addr_array[num_pages - 1], caching_type, true);
    if (readback != expected) {
        DLOG("Mismatch posting read: 0x%0llx != 0x%0llx", readback, expected);
        DASSERT(false);
    }
#else
    // Avoid compiler warnings
    (void)readback;
#endif

// TODO(MA-36) - gtt - remove GFX_FLSH_CNTL_GEN6 because not documented for bdw
#define GFX_FLSH_CNTL_GEN6 0x101008
#define GFX_FLSH_CNTL_EN (1 << 0)
    reg_io()->Write32(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
    reg_io()->mmio()->PostingRead32(GFX_FLSH_CNTL_GEN6);

    return true;
}
