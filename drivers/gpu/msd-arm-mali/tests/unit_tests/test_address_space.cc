// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_space.h"
#include "mock/mock_mmio.h"
#include "platform_mmio.h"
#include "registers.h"
#include "gtest/gtest.h"

class TestAddressSpace {
public:
    static mali_pte_t get_pte(AddressSpace* address_space, gpu_addr_t gpu_addr)
    {
        mali_pte_t result;
        EXPECT_TRUE(address_space->ReadPteForTesting(gpu_addr, &result));
        return result;
    }

    static void check_pte_entries_clear(AddressSpace* address_space, uint64_t gpu_addr,
                                        uint64_t size)
    {
        ASSERT_NE(address_space, nullptr);

        uint32_t page_count = size >> PAGE_SHIFT;

        for (unsigned int i = 0; i < page_count; i++) {
            uint64_t pte = get_pte(address_space, gpu_addr + i * PAGE_SIZE);
            EXPECT_EQ(2u, pte);
        }
    }

    static void check_pte_entries(AddressSpace* address_space, magma::PlatformBuffer* buffer,
                                  uint64_t gpu_addr, uint64_t flags)
    {
        ASSERT_NE(address_space, nullptr);

        ASSERT_TRUE(magma::is_page_aligned(buffer->size()));
        uint32_t page_count = buffer->size() / PAGE_SIZE;

        uint64_t bus_addr[page_count];
        EXPECT_TRUE(buffer->MapPageRangeBus(0, page_count, bus_addr));

        for (unsigned int i = 0; i < page_count; i++) {
            uint64_t pte = get_pte(address_space, gpu_addr + i * PAGE_SIZE);
            static constexpr uint64_t kFlagBits = (1l << 54) | (0xf << 6);
            EXPECT_EQ(pte & ~kFlagBits & ~(PAGE_SIZE - 1), bus_addr[i]);

            EXPECT_EQ(1u, pte & 3);
            EXPECT_EQ(flags, pte & kFlagBits);
        }
        EXPECT_TRUE(buffer->UnmapPageRangeBus(0, page_count));
    }

    static void Init()
    {
        auto address_space = AddressSpace::Create();

        check_pte_entries_clear(address_space.get(), 0, 1024);
    }

    static void Insert()
    {
        auto address_space = AddressSpace::Create();

        // create some buffers
        std::vector<uint64_t> addr = {PAGE_SIZE * 0xbdefcccef, PAGE_SIZE * 100};
        std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

        buffer[0] = magma::PlatformBuffer::Create(1000, "test");
        buffer[1] = magma::PlatformBuffer::Create(10000, "test");

        // Try to insert without pinning
        EXPECT_FALSE(address_space->Insert(addr[0], buffer[0].get(), 0, buffer[0]->size(), 0));

        EXPECT_TRUE(buffer[0]->PinPages(0, buffer[0]->size() / PAGE_SIZE));
        EXPECT_TRUE(buffer[1]->PinPages(0, buffer[1]->size() / PAGE_SIZE));

        // Correct
        EXPECT_TRUE(address_space->Insert(addr[0], buffer[0].get(), 0, buffer[0]->size(),
                                          kAccessFlagRead | kAccessFlagNoExecute));

        check_pte_entries(address_space.get(), buffer[0].get(), addr[0], (1 << 6) | (1l << 54));

        // Also correct
        EXPECT_TRUE(address_space->Insert(addr[1], buffer[1].get(), 0, buffer[1]->size(),
                                          kAccessFlagWrite | kAccessFlagShareBoth));

        check_pte_entries(address_space.get(), buffer[1].get(), addr[1], (2 << 8) | (1 << 7));

        auto page_directory = address_space->root_page_directory_.get();
        for (int i = 3; i >= 0; i--) {
            uint64_t offset = (addr[0] >> (9 * i + PAGE_SHIFT)) & AddressSpace::kPageTableMask;

            uint64_t entry_flags = i > 0 ? 3u : 1u;
            EXPECT_EQ(entry_flags, page_directory->gpu()->entry[offset] & 3u);
            EXPECT_TRUE(page_directory->gpu()->entry[offset] & ~511);
            if (i > 0)
                page_directory = page_directory->next_levels_[offset].get();
            else
                EXPECT_EQ(0u, page_directory->next_levels_.size());
        }

        EXPECT_TRUE(address_space->Clear(addr[1], buffer[1]->size()));

        check_pte_entries_clear(address_space.get(), addr[1], buffer[1]->size());

        EXPECT_TRUE(address_space->Clear(addr[0], buffer[0]->size()));

        check_pte_entries_clear(address_space.get(), addr[0], buffer[0]->size());

        // Clear entries that don't exist yet.
        EXPECT_TRUE(address_space->Clear(PAGE_SIZE * 1024, PAGE_SIZE * 5));

        EXPECT_TRUE(address_space->Clear((1l << 48) - PAGE_SIZE * 10, PAGE_SIZE * 10));

        // Extend outside of address space.
        EXPECT_FALSE(address_space->Clear((1l << 48) - PAGE_SIZE * 10, PAGE_SIZE * 11));

        EXPECT_FALSE(address_space->Insert((1l << 48) - PAGE_SIZE, buffer[1].get(), 0,
                                           buffer[1]->size(),
                                           kAccessFlagRead | kAccessFlagNoExecute));
    }
};

TEST(AddressSpace, Init) { TestAddressSpace::Init(); }

TEST(AddressSpace, Insert) { TestAddressSpace::Insert(); }
