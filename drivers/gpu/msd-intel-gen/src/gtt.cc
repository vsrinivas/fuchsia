// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtt.h"
#include "magma_util/macros.h"
#include "magma_util/simple_allocator.h"
#include "registers.h"
#include <vector>

static inline gen_pte_t gen_pte_encode(uint64_t bus_addr, bool valid)
{
    gen_pte_t pte = bus_addr | PAGE_RW;
    if (valid)
        pte |= PAGE_PRESENT;

    return pte;
}

Gtt::Gtt() : AddressSpace(ADDRESS_SPACE_GGTT) {}

bool Gtt::Init(uint64_t gtt_size, magma::PlatformDevice* platform_device)
{
    // address space size
    size_ = (gtt_size / sizeof(gen_pte_t)) * PAGE_SIZE;

    DLOG("Gtt::Init gtt_size (for page tables) 0x%lx size (address space) 0x%lx ", gtt_size, size_);

    if (!MapGttMmio(platform_device))
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

bool Gtt::InitPageTables(uint64_t start)
{
    // leave space for a guard page
    allocator_ = magma::SimpleAllocator::Create(start, size_ - PAGE_SIZE);
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

bool Gtt::InitScratch()
{
    scratch_ = magma::PlatformBuffer::Create(PAGE_SIZE);

    if (!scratch_->PinPages(0, 1))
        return DRETF(false, "PinPages failed");

    if (!scratch_->MapPageRangeBus(0, 1, &scratch_bus_addr_))
        return DRETF(false, "MapPageBus failed");

    if (magma::kDebug) {
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

    return true;
}

bool Gtt::Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out)
{
    DASSERT(allocator_);
    // allocate an extra page on the end to avoid page faults from over fetch
    // see
    // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-skl-vol02a-commandreference-instructions.pdf
    // page 908
    size_t alloc_size = size + PAGE_SIZE;
    return allocator_->Alloc(alloc_size, align_pow2, addr_out);
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

    DLOG("first_entry 0x%lx num_entries %ld max_entries %ld", first_entry, num_entries,
         max_entries);

    if (first_entry + num_entries > max_entries)
        return DRETF(false, "exceeded max_entries");

    gen_pte_t pte = gen_pte_encode(scratch_bus_addr_, false);

    uint64_t pte_offset = pte_mmio_offset() + first_entry * sizeof(gen_pte_t);

    for (unsigned int i = 0; i < num_entries; i++) {
        mmio_->Write64(static_cast<uint64_t>(pte), pte_offset + i * sizeof(gen_pte_t));
    }

    mmio_->PostingRead32(pte_offset + (num_entries - 1) * sizeof(gen_pte_t));

    return true;
}

bool Gtt::Insert(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t offset, uint64_t length,
                 CachingType caching_type)
{
    DLOG("InsertEntries addr 0x%lx", addr);

    DASSERT(magma::is_page_aligned(offset));
    DASSERT(magma::is_page_aligned(length));

    size_t allocated_length;
    if (!allocator_->GetSize(addr, &allocated_length))
        return DRETF(false, "couldn't get allocated length for addr");

    // add an extra page to length to account for the overfetch protection page
    if (length + PAGE_SIZE != allocated_length)
        return DRETF(false, "allocated length (0x%zx) doesn't match length (0x%" PRIx64 ")",
                     allocated_length, length);

    uint32_t start_page_index = offset / PAGE_SIZE;
    uint32_t num_pages = length / PAGE_SIZE;

    DLOG("start_page_index 0x%x num_pages 0x%x", start_page_index, num_pages);

    uint64_t first_entry = addr >> PAGE_SHIFT;
    uint64_t pte_offset = pte_mmio_offset() + first_entry * sizeof(gen_pte_t);

    std::vector<uint64_t> bus_addr_array;
    bus_addr_array.resize(num_pages);

    if (!buffer->MapPageRangeBus(start_page_index, num_pages, bus_addr_array.data()))
        return DRETF(false, "failed obtaining bus addresses");

    for (unsigned int i = 0; i < num_pages; i++) {
        auto pte = gen_pte_encode(bus_addr_array[i], true);
        mmio_->Write64(static_cast<uint64_t>(pte), pte_offset + i * sizeof(gen_pte_t));
    }

    // insert pte for overfetch protection page
    auto pte = gen_pte_encode(scratch_bus_addr_, true);
    mmio_->Write64(static_cast<uint64_t>(pte), pte_offset + (num_pages) * sizeof(gen_pte_t));

    uint64_t readback = mmio_->PostingRead64(pte_offset + (num_pages - 1) * sizeof(gen_pte_t));

    if (magma::kDebug) {
        auto expected = gen_pte_encode(bus_addr_array[num_pages - 1], true);
        if (readback != expected) {
            DLOG("Mismatch posting read: 0x%0lx != 0x%0lx", readback, expected);
            DASSERT(false);
        }
    }

    return true;
}
