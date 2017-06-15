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

static inline gen_pde_t gen_pde_encode(uint64_t bus_addr)
{
    return bus_addr | PAGE_RW | PAGE_PRESENT;
}

std::unique_ptr<PerProcessGtt::PageDirectory> PerProcessGtt::PageDirectory::Create()
{
    static_assert(offsetof(PageDirectoryGpu, entry) == 0,
                  "unexpected offsetof(PageDirectory,entry)");
    static_assert(offsetof(PageDirectoryGpu, page_table[0]) == PAGE_SIZE,
                  "unexpected offsetof(PageDirectory,page_table[0])");
    static_assert(offsetof(PageDirectoryGpu, page_table[1]) == 2 * PAGE_SIZE,
                  "unexpected offsetof(PageDirectory,page_table[1])");

    const uint32_t kPageCount = magma::round_up(sizeof(PageDirectoryGpu), PAGE_SIZE) / PAGE_SIZE;

    auto buffer = magma::PlatformBuffer::Create(kPageCount * PAGE_SIZE, "ppgtt-directory");
    if (!buffer)
        return DRETP(nullptr, "couldn't create buffer");

    if (!buffer->PinPages(0, kPageCount))
        return DRETP(nullptr, "failed to pin pages");

    PageDirectoryGpu* gpu;
    if (!buffer->MapCpu(reinterpret_cast<void**>(&gpu)))
        return DRETP(nullptr, "failed to map cpu");

    std::vector<uint64_t> page_bus_addresses(kPageCount);
    if (!buffer->MapPageRangeBus(0, kPageCount, page_bus_addresses.data()))
        return DRETP(nullptr, "failed to map page range bus");

    return std::unique_ptr<PageDirectory>(
        new PageDirectory(std::move(buffer), gpu, std::move(page_bus_addresses)));
}

PerProcessGtt::PageDirectory::PageDirectory(std::unique_ptr<magma::PlatformBuffer> buffer,
                                            PageDirectoryGpu* gpu,
                                            std::vector<uint64_t> page_bus_addresses)
    : buffer_(std::move(buffer)), gpu_(gpu), page_bus_addresses_(std::move(page_bus_addresses))
{
    bus_addr_ = page_bus_addresses_[0];

    for (uint32_t entry = 0; entry < kPageDirectoryEntries; entry++) {
        uint32_t page_index = entry + 1;
        DASSERT(page_index < page_bus_addresses_.size());
        write_pde(entry, gen_pde_encode(page_bus_addresses_[page_index]));
    }
}

//////////////////////////////////////////////////////////////////////////////

std::unique_ptr<PerProcessGtt>
PerProcessGtt::Create(std::shared_ptr<magma::PlatformBuffer> scratch_buffer,
                      std::shared_ptr<GpuMappingCache> cache)
{
    std::vector<std::unique_ptr<PageDirectory>> page_directories(kPageDirectories);

    for (uint32_t i = 0; i < page_directories.size(); i++) {
        auto page_directory = PageDirectory::Create();
        if (!page_directory)
            return DRETP(nullptr, "couldn't create page directory %d", i);
        page_directories[i] = std::move(page_directory);
    }

    return std::unique_ptr<PerProcessGtt>(new PerProcessGtt(
        std::move(scratch_buffer), std::move(page_directories), std::move(cache)));
}

PerProcessGtt::PerProcessGtt(std::shared_ptr<magma::PlatformBuffer> scratch_buffer,
                             std::vector<std::unique_ptr<PageDirectory>> page_directories,
                             std::shared_ptr<GpuMappingCache> cache)
    : AddressSpace(ADDRESS_SPACE_PPGTT, cache), scratch_buffer_(std::move(scratch_buffer)),
      page_directories_(std::move(page_directories))
{
}

// Called lazily by Alloc
bool PerProcessGtt::Init()
{
    DASSERT(!initialized_);
    DASSERT(page_directories_.size() == kPageDirectories);

    if (!scratch_buffer_->MapPageRangeBus(0, 1, &scratch_bus_addr_))
        return DRETF(false, "MapPageBus failed");

    uint64_t start = 0;

    allocator_ = magma::SimpleAllocator::Create(start, Size());
    if (!allocator_)
        return DRETF(false, "failed to create allocator");

    initialized_ = true;

    bool result = Clear(start, Size());
    DASSERT(result);

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
    gen_pte_t pte = gen_pte_encode(scratch_bus_addr_, CACHING_NONE, true, false);

    uint32_t page_table_index = (start >> PAGE_SHIFT) & kPageTableMask;
    uint32_t page_directory_index = (start >> (PAGE_SHIFT + kPageTableShift)) & kPageDirectoryMask;
    uint32_t page_directory_pointer_index =
        start >> (PAGE_SHIFT + kPageTableShift + kPageDirectoryShift);

    DLOG("start pdp %u pd %i pt %u", page_directory_pointer_index, page_directory_index,
         page_table_index);

    for (uint64_t num_entries = length >> PAGE_SHIFT; num_entries > 0; num_entries--) {
        DASSERT(page_directory_pointer_index < kPageDirectories);
        page_directories_[page_directory_pointer_index]->write_pte(page_directory_index,
                                                                   page_table_index, pte);

        if (++page_table_index == kPageTableEntries) {
            page_table_index = 0;
            if (++page_directory_index == kPageDirectoryEntries) {
                page_directory_index = 0;
                ++page_directory_pointer_index;
            }
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

    uint32_t page_table_index = (addr >> PAGE_SHIFT) & kPageTableMask;
    uint32_t page_directory_index = (addr >> (PAGE_SHIFT + kPageTableShift)) & kPageDirectoryMask;
    uint32_t page_directory_pointer_index =
        addr >> (PAGE_SHIFT + kPageTableShift + kPageDirectoryShift);

    DLOG("start pdp %u pd %i pt %u", page_directory_pointer_index, page_directory_index,
         page_table_index);

    for (uint64_t i = 0; i < num_pages + kOverfetchPageCount + kGuardPageCount; i++) {
        if (i < num_pages) {
            // buffer pages
            gen_pte_t pte = gen_pte_encode(bus_addr_array[i], caching_type, true, true);
            page_directories_[page_directory_pointer_index]->write_pte(page_directory_index,
                                                                       page_table_index, pte);
        } else if (i < num_pages + kOverfetchPageCount) {
            // overfetch page: readable
            gen_pte_t pte = gen_pte_encode(scratch_bus_addr_, CACHING_NONE, true, false);
            page_directories_[page_directory_pointer_index]->write_pte(page_directory_index,
                                                                       page_table_index, pte);
        } else {
            // guard page: also readable, because mesa doesn't properly handle overfetching
            gen_pte_t pte = gen_pte_encode(scratch_bus_addr_, CACHING_NONE, true, false);
            page_directories_[page_directory_pointer_index]->write_pte(page_directory_index,
                                                                       page_table_index, pte);
        }

        if (++page_table_index == kPageTableEntries) {
            page_table_index = 0;
            if (++page_directory_index == kPageDirectoryEntries) {
                page_directory_index = 0;
                ++page_directory_pointer_index;
                DASSERT(page_directory_pointer_index <= kPageDirectories);
            }
        }
    }
    return true;
}

PerProcessGtt::PageDirectoryGpu*
PerProcessGtt::get_page_directory_gpu(uint32_t page_directory_pointer_index)
{
    DASSERT(page_directory_pointer_index < page_directories_.size());
    return page_directories_[page_directory_pointer_index]->gpu();
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
