// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppgtt.h"
#include "magma_util/macros.h"
#include "magma_util/simple_allocator.h"
#include "platform_buffer.h"
#include "registers.h"

constexpr bool kLogEnable = false;

static unsigned int gen_ppat_index(CachingType caching_type)
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

static inline gen_pte_t gen_pte_encode(uint64_t bus_addr, CachingType caching_type, bool valid,
                                       bool writeable)
{
    gen_pte_t pte = bus_addr;

    if (valid)
        pte |= PAGE_PRESENT;

    if (writeable)
        pte |= PAGE_RW;

    unsigned int pat_index = gen_ppat_index(caching_type);
    if (pat_index & (1 << 0))
        pte |= PAGE_PWT;
    if (pat_index & (1 << 1))
        pte |= PAGE_PCD;
    if (pat_index & (1 << 2))
        pte |= PAGE_PAT;

    return pte;
}

//////////////////////////////////////////////////////////////////////////////

std::unique_ptr<PerProcessGtt> PerProcessGtt::Create(std::shared_ptr<GpuMappingCache> cache)
{
    auto scratch_page = magma::PlatformBuffer::Create(PAGE_SIZE, "scratch");
    if (!scratch_page)
        return DRETP(nullptr, "couldn't allocate scratch page");

    if (!scratch_page->PinPages(0, 1))
        return DRETP(nullptr, "failed to pin scratch page");

    uint64_t scratch_bus_addr;
    if (!scratch_page->MapPageRangeBus(0, 1, &scratch_bus_addr))
        return DRETP(nullptr, "MapPageRangeBus failed");

    auto pml4_table = Pml4Table::Create(std::move(scratch_page), scratch_bus_addr);
    if (!pml4_table)
        return DRETP(nullptr, "failed to create pml4table");

    return std::unique_ptr<PerProcessGtt>(
        new PerProcessGtt(std::move(pml4_table), std::move(cache)));
}

PerProcessGtt::PerProcessGtt(std::unique_ptr<Pml4Table> pml4_table,
                             std::shared_ptr<GpuMappingCache> cache)
    : AddressSpace(ADDRESS_SPACE_PPGTT, cache), pml4_table_(std::move(pml4_table))
{
}

// Called lazily by Alloc
bool PerProcessGtt::Init()
{
    DASSERT(!initialized_);

    uint64_t start = 0;

    allocator_ = magma::SimpleAllocator::Create(start, Size());
    if (!allocator_)
        return DRETF(false, "failed to create allocator");

    initialized_ = true;

    return true;
}

bool PerProcessGtt::Clear(uint64_t addr)
{
    DASSERT(initialized_);
    DASSERT(allocator_);
    size_t length;
    if (!allocator_->GetSize(addr, &length))
        return DRETF(false, "couldn't get size for addr");
    if (!Clear(addr, length))
        return DRETF(false, "clear failed");
    return true;
}

bool PerProcessGtt::Clear(uint64_t start, uint64_t length)
{
    DASSERT(initialized_);
    DASSERT((start & (PAGE_SIZE - 1)) == 0);
    DASSERT((length & (PAGE_SIZE - 1)) == 0);

    if (start > Size())
        return DRETF(false, "invalid start");

    if (start + length > Size())
        return DRETF(false, "invalid start + length");

    // readable, because mesa doesn't properly handle overfetching
    gen_pte_t pte = gen_pte_encode(pml4_table_->scratch_page_bus_addr(), CACHING_NONE, true, false);

    uint32_t page_table_index = (start >>= PAGE_SHIFT) & kPageTableMask;
    uint32_t page_directory_index = (start >>= kPageTableShift) & kPageDirectoryMask;
    uint32_t page_directory_ptr_index = (start >>= kPageDirectoryShift) & kPageDirectoryPtrMask;
    uint32_t pml4_index = (start >>= kPageDirectoryPtrShift);

    DLOG("start pml4 %u pdp %u pd %i pt %u", pml4_index, page_directory_ptr_index,
         page_directory_index, page_table_index);

    auto page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
    auto page_table_entry =
        page_directory ? page_directory->page_table_entry(page_directory_index, page_table_index)
                       : nullptr;

    for (uint64_t num_entries = length >> PAGE_SHIFT; num_entries > 0; num_entries--) {
        if (!page_table_entry)
            return DRETF(false, "couldn't get page table entry");

        *page_table_entry++ = pte;

        if (++page_table_index == kPageTableEntries) {
            page_table_index = 0;
            if (++page_directory_index == kPageDirectoryEntries) {
                page_directory_index = 0;
                if (++page_directory_ptr_index == kPageDirectoryPtrEntries) {
                    page_directory_ptr_index = 0;
                    ++pml4_index;
                    DASSERT(pml4_index < kPml4Entries);
                }
                page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
            }
            page_table_entry =
                page_directory
                    ? page_directory->page_table_entry(page_directory_index, page_table_index)
                    : nullptr;
        }
    }

    return true;
}

bool PerProcessGtt::Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out)
{
    if (!initialized_ && !Init())
        return DRETF(false, "failed to initialize");

    DASSERT(allocator_);
    // allocate an extra page on the end to avoid page faults from over fetch
    // see
    // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-skl-vol02a-commandreference-instructions.pdf
    // page 908
    size_t alloc_size = size + (kOverfetchPageCount + kGuardPageCount) * PAGE_SIZE;
    return allocator_->Alloc(alloc_size, align_pow2, addr_out);
}

bool PerProcessGtt::Free(uint64_t addr)
{
    DASSERT(initialized_);
    DASSERT(allocator_);

    size_t length;
    if (!allocator_->GetSize(addr, &length))
        return DRETF(false, "couldn't find length for addr 0x%" PRIx64, addr);

    if (kLogEnable)
        magma::log(magma::LOG_INFO, "ppgtt free (%p) 0x%" PRIx64 "-0x%" PRIx64 " length 0x%" PRIx64,
                   this, addr, addr + length - 1, length);

    return allocator_->Free(addr);
}

bool PerProcessGtt::Insert(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t offset,
                           uint64_t length, CachingType caching_type)
{
    if (kLogEnable)
        magma::log(magma::LOG_INFO,
                   "ppgtt insert (%p) 0x%" PRIx64 "-0x%" PRIx64 " length 0x%" PRIx64, this, addr,
                   addr + length - 1, length);

    DASSERT(initialized_);
    DASSERT(magma::is_page_aligned(offset));
    DASSERT(magma::is_page_aligned(length));

    size_t allocated_length;
    if (!allocator_->GetSize(addr, &allocated_length))
        return DRETF(false, "couldn't get allocated length for addr");

    // add extra pages to length to account for overfetch and guard pages
    if (length + (kOverfetchPageCount + kGuardPageCount) * PAGE_SIZE != allocated_length)
        return DRETF(false, "allocated length (0x%zx) doesn't match length (0x%" PRIx64 ")",
                     allocated_length, length);

    uint32_t start_page_index = offset / PAGE_SIZE;
    uint32_t num_pages = length / PAGE_SIZE;

    DLOG("start_page_index 0x%x num_pages 0x%x", start_page_index, num_pages);

    std::vector<uint64_t> bus_addr_array;
    bus_addr_array.resize(num_pages);

    if (!buffer->MapPageRangeBus(start_page_index, num_pages, bus_addr_array.data()))
        return DRETF(false, "failed obtaining bus addresses");

    uint32_t page_table_index = (addr >>= PAGE_SHIFT) & kPageTableMask;
    uint32_t page_directory_index = (addr >>= kPageTableShift) & kPageDirectoryMask;
    uint32_t page_directory_ptr_index = (addr >>= kPageDirectoryShift) & kPageDirectoryPtrMask;
    uint32_t pml4_index = (addr >>= kPageDirectoryPtrShift);

    DLOG("addr pml4 %u pdp %u pd %i pt %u", pml4_index, page_directory_ptr_index,
         page_directory_index, page_table_index);

    auto page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
    auto page_table_entry =
        page_directory ? page_directory->page_table_entry(page_directory_index, page_table_index)
                       : nullptr;

    for (uint64_t i = 0; i < num_pages + kOverfetchPageCount + kGuardPageCount; i++) {
        gen_pte_t pte;
        if (i < num_pages) {
            // buffer pages
            pte = gen_pte_encode(bus_addr_array[i], caching_type, true, true);
        } else if (i < num_pages + kOverfetchPageCount) {
            // overfetch page: readable
            pte = gen_pte_encode(pml4_table_->scratch_page_bus_addr(), CACHING_NONE, true, false);
        } else {
            // guard page: also readable, because mesa doesn't properly handle overfetching
            pte = gen_pte_encode(pml4_table_->scratch_page_bus_addr(), CACHING_NONE, true, false);
        }

        if (!page_table_entry)
            return DRETF(false, "couldn't get page table entry");

        *page_table_entry++ = pte;

        if (++page_table_index == kPageTableEntries) {
            page_table_index = 0;
            if (++page_directory_index == kPageDirectoryEntries) {
                page_directory_index = 0;
                if (++page_directory_ptr_index == kPageDirectoryPtrEntries) {
                    page_directory_ptr_index = 0;
                    ++pml4_index;
                    DASSERT(pml4_index < kPml4Entries);
                }
                page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
            }
            page_table_entry =
                page_directory
                    ? page_directory->page_table_entry(page_directory_index, page_table_index)
                    : nullptr;
        }
    }

    return true;
}

gen_pte_t PerProcessGtt::get_pte(gpu_addr_t gpu_addr)
{
    gpu_addr_t addr_copy = gpu_addr;

    uint32_t page_table_index = (addr_copy >>= PAGE_SHIFT) & PerProcessGtt::kPageTableMask;
    uint32_t page_directory_index =
        (addr_copy >>= PerProcessGtt::kPageTableShift) & PerProcessGtt::kPageDirectoryMask;
    uint32_t page_directory_ptr_index =
        (addr_copy >>= PerProcessGtt::kPageDirectoryShift) & PerProcessGtt::kPageDirectoryPtrMask;
    uint32_t pml4_index = (addr_copy >>= PerProcessGtt::kPageDirectoryPtrShift);

    DLOG("gpu_addr 0x%lx pml4 0x%x pdp 0x%x pd 0x%x pt 0x%x", gpu_addr, pml4_index,
         page_directory_ptr_index, page_directory_index, page_table_index);

    auto page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
    DASSERT(page_directory);
    auto page_table_entry =
        page_directory->page_table_entry(page_directory_index, page_table_index);
    DASSERT(page_table_entry);

    return *page_table_entry;
}

//////////////////////////////////////////////////////////////////////////////

// Initialize the private page attribute registers, used to define the meaning
// of the pat bits in the page table entries.
void PerProcessGtt::InitPrivatePat(RegisterIo* reg_io)
{
    DASSERT(gen_ppat_index(CACHING_WRITE_THROUGH) == 2);
    DASSERT(gen_ppat_index(CACHING_NONE) == 3);
    DASSERT(gen_ppat_index(CACHING_LLC) == 4);

    uint64_t pat =
        registers::PatIndex::ppat(0, registers::PatIndex::kLruAgeFromUncore,
                                  registers::PatIndex::kLlc, registers::PatIndex::kWriteBack);
    pat |= registers::PatIndex::ppat(1, registers::PatIndex::kLruAgeFromUncore,
                                     registers::PatIndex::kLlcEllc,
                                     registers::PatIndex::kWriteCombining);
    pat |= registers::PatIndex::ppat(2, registers::PatIndex::kLruAgeFromUncore,
                                     registers::PatIndex::kLlcEllc,
                                     registers::PatIndex::kWriteThrough);
    pat |= registers::PatIndex::ppat(3, registers::PatIndex::kLruAgeFromUncore,
                                     registers::PatIndex::kEllc, registers::PatIndex::kUncacheable);
    pat |=
        registers::PatIndex::ppat(4, registers::PatIndex::kLruAgeFromUncore,
                                  registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteBack);
    pat |=
        registers::PatIndex::ppat(5, registers::PatIndex::kLruAgeZero,
                                  registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteBack);
    pat |=
        registers::PatIndex::ppat(6, registers::PatIndex::kLruAgeNoChange,
                                  registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteBack);
    pat |=
        registers::PatIndex::ppat(7, registers::PatIndex::kLruAgeThree,
                                  registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteBack);

    registers::PatIndex::write(reg_io, pat);
}
