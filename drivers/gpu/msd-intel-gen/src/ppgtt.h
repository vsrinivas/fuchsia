// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPGTT_H
#define PPGTT_H

#include "address_space.h"
#include "magma_util/address_space_allocator.h"
#include "platform_buffer.h"
#include "register_io.h"
#include <memory>
#include <vector>

using gen_pde_t = uint64_t;

class PerProcessGtt : public AddressSpace {
public:
    // Create with the given scratch_buffer, which should be one page that has already been pinned.
    static std::unique_ptr<PerProcessGtt>
    Create(std::shared_ptr<magma::PlatformBuffer> scratch_buffer,
           std::shared_ptr<GpuMappingCache> cache);

    uint64_t Size() const override { return kSize; }

    static void InitPrivatePat(RegisterIo* reg_io);

    // AddressSpace overrides
    bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) override;
    bool Free(uint64_t addr) override;

    bool Clear(uint64_t addr) override;
    bool Insert(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t offset, uint64_t length,
                CachingType caching_type) override;

    uint64_t get_pdp(uint32_t index)
    {
        DASSERT(index < page_directories_.size());
        return page_directories_[index]->bus_addr();
    }

private:
    class PageDirectory; // defined below

    PerProcessGtt(std::shared_ptr<magma::PlatformBuffer> scratch_buffer,
                  std::vector<std::unique_ptr<PageDirectory>> page_directories,
                  std::shared_ptr<GpuMappingCache> cache);

    // Legacy 32-bit ppgtt = 4 PDP registers; each PD handles 1GB (512 * 512 * 4096) = 4GB total
    static constexpr uint64_t kPageDirectories = 4; // aka page directory pointer entries

    static constexpr uint64_t kPageDirectoryShift = 9;
    static constexpr uint64_t kPageDirectoryEntries = 1 << kPageDirectoryShift;
    static constexpr uint64_t kPageDirectoryMask = kPageDirectoryEntries - 1;

    static constexpr uint64_t kPageTableShift = 9;
    static constexpr uint64_t kPageTableEntries = 1 << kPageTableShift;
    static constexpr uint64_t kPageTableMask = kPageTableEntries - 1;

    static constexpr uint64_t kSize =
        kPageDirectories * kPageDirectoryEntries * kPageTableEntries * PAGE_SIZE;

    static constexpr uint32_t kOverfetchPageCount = 1;
    static constexpr uint32_t kGuardPageCount = 8;

    static_assert(kSize == 1ull << 32, "ppgtt size calculation");

    bool Init();
    bool Clear(uint64_t start, uint64_t length);

    struct PageTableGpu {
        gen_pte_t entry[kPageTableEntries];
    };

    struct PageDirectoryGpu {
        gen_pde_t entry[kPageDirectoryEntries];
        PageTableGpu page_table[kPageDirectoryEntries];
    };

    class PageDirectory {
    public:
        static std::unique_ptr<PageDirectory> Create();

        void write_pte(uint32_t page_directory_index, uint32_t page_table_index,
                       gen_pte_t page_table_entry)
        {
            DASSERT(page_directory_index < kPageDirectoryEntries);
            DASSERT(page_table_index < kPageTableEntries);
            gpu_->page_table[page_directory_index].entry[page_table_index] = page_table_entry;
        }

        uint64_t bus_addr() { return bus_addr_; }

        PageDirectoryGpu* gpu() { return gpu_; }

    private:
        PageDirectory(std::unique_ptr<magma::PlatformBuffer> buffer, PageDirectoryGpu* gpu,
                      std::vector<uint64_t> page_bus_addresses);

        void write_pde(uint32_t index, gen_pde_t pde)
        {
            DASSERT(index < kPageDirectoryEntries);
            gpu_->entry[index] = pde;
        }

        std::unique_ptr<magma::PlatformBuffer> buffer_;
        PageDirectoryGpu* gpu_;
        std::vector<uint64_t> page_bus_addresses_;
        std::shared_ptr<magma::PlatformBuffer> scratch_buffer_;
        uint64_t bus_addr_;
    };

    bool initialized_ = false;
    std::shared_ptr<magma::PlatformBuffer> scratch_buffer_;
    std::vector<std::unique_ptr<PageDirectory>> page_directories_;
    std::unique_ptr<magma::AddressSpaceAllocator> allocator_;
    uint64_t scratch_bus_addr_{};

    // For testing
    friend class TestPerProcessGtt;
    PageDirectoryGpu* get_page_directory_gpu(uint32_t page_directory_pointer_index);
};

#endif // PPGTT_H
