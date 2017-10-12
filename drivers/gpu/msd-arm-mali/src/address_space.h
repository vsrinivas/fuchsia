// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_SPACE_H_
#define ADDRESS_SPACE_H_

#include <limits.h>

#include <vector>

#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "types.h"

#ifndef PAGE_SHIFT

#if PAGE_SIZE == 4096
#define PAGE_SHIFT 12
#else
#error PAGE_SHIFT not defined
#endif

#endif

// This should only be accessed on the connection thread (for now).
class AddressSpace {
public:
    static constexpr uint32_t kVirtualAddressSize = 48;

    static std::unique_ptr<AddressSpace> Create();

    bool Insert(gpu_addr_t addr, magma::PlatformBuffer* buffer, uint64_t offset, uint64_t length,
                uint64_t flags);

    bool Clear(gpu_addr_t start, uint64_t length);

    bool ReadPteForTesting(gpu_addr_t addr, mali_pte_t* entry);

private:
    static constexpr uint32_t kPageTableEntries = PAGE_SIZE / sizeof(mali_pte_t);
    static constexpr uint32_t kPageTableMask = kPageTableEntries - 1;
    static constexpr uint32_t kPageOffsetBits = 9;
    static_assert(kPageTableEntries == 1 << kPageOffsetBits, "incorrect page table entry count");
    // There are 3 levels of page directories, then an address table.
    static constexpr uint32_t kPageDirectoryLevels = 4;

    static_assert(kPageOffsetBits * kPageDirectoryLevels + PAGE_SHIFT == kVirtualAddressSize,
                  "Incorrect virtual address size");

    struct PageTableGpu {
        mali_pte_t entry[kPageTableEntries];
    };

    class PageTable {
    public:
        static std::unique_ptr<PageTable> Create(uint32_t level);

        PageTableGpu* gpu() { return gpu_; }

        // Get the leaf page table for |page_number|. If |create| is false then
        // returns null instead of creating one.
        PageTable* GetPageTableLevel0(uint64_t page_number, bool create);

        void WritePte(uint64_t page_index, mali_pte_t pte);

    private:
        static mali_pte_t get_directory_entry(uint64_t physical_address);

        PageTable(uint32_t level, std::unique_ptr<magma::PlatformBuffer> buffer, PageTableGpu* gpu,
                  uint64_t page_bus_address);
        uint64_t page_bus_address() { return page_bus_address_; }

        // The root page table has level 3, and the leaves have level 0.
        uint32_t level_;
        std::unique_ptr<magma::PlatformBuffer> buffer_;
        PageTableGpu* gpu_;
        uint64_t page_bus_address_;
        std::vector<std::unique_ptr<PageTable>> next_levels_;

        friend class TestAddressSpace;
    };

    AddressSpace(std::unique_ptr<PageTable> root_page_directory);

    std::unique_ptr<PageTable> root_page_directory_;

    friend class TestAddressSpace;

    DISALLOW_COPY_AND_ASSIGN(AddressSpace);
};

#endif
