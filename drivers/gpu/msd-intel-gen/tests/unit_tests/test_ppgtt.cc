// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_mmio.h"
#include "platform_mmio.h"
#include "ppgtt.h"
#include "registers.h"
#include "gtest/gtest.h"

class TestPerProcessGtt {
public:
    static uint32_t cache_bits(CachingType caching_type)
    {
        switch (caching_type) {
        case CACHING_NONE:
            return (1 << 3) | (1 << 4); // 3
        case CACHING_WRITE_THROUGH:
            return (1 << 4); // 3
        case CACHING_LLC:
            return 1 << 7; // 4
        }
    }

    static gen_pte_t get_pte(PerProcessGtt* ppgtt, gpu_addr_t gpu_addr)
    {
        uint32_t page_table_index = (gpu_addr >> PAGE_SHIFT) & PerProcessGtt::kPageTableMask;
        uint32_t page_directory_index =
            (gpu_addr >> (PAGE_SHIFT + PerProcessGtt::kPageTableShift)) &
            PerProcessGtt::kPageDirectoryMask;
        uint32_t page_directory_pointer_index =
            gpu_addr >>
            (PAGE_SHIFT + PerProcessGtt::kPageTableShift + PerProcessGtt::kPageDirectoryShift);

        PerProcessGtt::PageDirectoryGpu* page_directory_gpu =
            ppgtt->get_page_directory_gpu(page_directory_pointer_index);
        return page_directory_gpu->page_table[page_directory_index].entry[page_table_index];
    }

    static void check_pte_entries_clear(PerProcessGtt* ppgtt, uint64_t gpu_addr, uint64_t size,
                                        uint64_t bus_addr)
    {
        ASSERT_NE(ppgtt, nullptr);

        uint32_t page_count = size >> PAGE_SHIFT;

        // Note: <= is intentional here to accout for ofer-fetch protection page
        for (unsigned int i = 0; i <= page_count; i++) {
            uint64_t pte = get_pte(ppgtt, gpu_addr + i * PAGE_SIZE);
            EXPECT_EQ(pte & ~(PAGE_SIZE - 1), bus_addr);
            EXPECT_FALSE(pte & 0x1); // page should not be present
            EXPECT_TRUE(pte & 0x3);  // rw
            EXPECT_EQ(pte & cache_bits(CACHING_NONE), cache_bits(CACHING_NONE));
        }
    }

    static void check_pte_entries(PerProcessGtt* ppgtt, magma::PlatformBuffer* buffer,
                                  uint64_t gpu_addr, uint64_t scratch_bus_addr,
                                  CachingType caching_type)
    {
        ASSERT_NE(ppgtt, nullptr);

        ASSERT_TRUE(magma::is_page_aligned(buffer->size()));
        uint32_t page_count = buffer->size() / PAGE_SIZE;

        uint64_t bus_addr[page_count];
        EXPECT_TRUE(buffer->MapPageRangeBus(0, page_count, bus_addr));

        for (unsigned int i = 0; i < page_count; i++) {
            uint64_t pte = get_pte(ppgtt, gpu_addr + i * PAGE_SIZE);
            EXPECT_EQ(pte & ~(PAGE_SIZE - 1), bus_addr[i]);
            EXPECT_TRUE(pte & 0x1); // page present
            EXPECT_TRUE(pte & 0x3); // rw
            EXPECT_EQ(pte & cache_bits(caching_type), cache_bits(caching_type));
        }
        EXPECT_TRUE(buffer->UnmapPageRangeBus(0, page_count));

        uint64_t pte = get_pte(ppgtt, gpu_addr + page_count * PAGE_SIZE);
        EXPECT_EQ(pte & ~(PAGE_SIZE - 1), scratch_bus_addr);
        EXPECT_TRUE(pte & 0x1); // page present
        EXPECT_TRUE(pte & 0x3); // rw
        EXPECT_EQ(pte & cache_bits(CACHING_NONE), cache_bits(CACHING_NONE));
    }

    static std::shared_ptr<magma::PlatformBuffer> get_scratch_buffer()
    {
        std::shared_ptr<magma::PlatformBuffer> scratch_buffer =
            magma::PlatformBuffer::Create(PAGE_SIZE);
        if (!scratch_buffer)
            return nullptr;
        if (!scratch_buffer->PinPages(0, 1))
            return nullptr;
        return scratch_buffer;
    }

    static void Init()
    {
        auto scratch_buffer = get_scratch_buffer();

        auto ppgtt = PerProcessGtt::Create(scratch_buffer, GpuMappingCache::Create());

        EXPECT_TRUE(ppgtt->Init());

        uint64_t scratch_bus_addr;
        EXPECT_TRUE(scratch_buffer->MapPageRangeBus(0, 1, &scratch_bus_addr));

        check_pte_entries_clear(ppgtt.get(), 0, ppgtt->Size(), scratch_bus_addr);

        EXPECT_TRUE(scratch_buffer->UnmapPageRangeBus(0, 1));
    }

    static void Insert()
    {
        auto scratch_buffer = get_scratch_buffer();

        auto ppgtt = PerProcessGtt::Create(scratch_buffer, GpuMappingCache::Create());
        EXPECT_TRUE(ppgtt->Init());

        uint64_t scratch_bus_addr;
        EXPECT_TRUE(scratch_buffer->MapPageRangeBus(0, 1, &scratch_bus_addr));

        // create some buffers
        std::vector<uint64_t> addr(2);
        std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

        buffer[0] = magma::PlatformBuffer::Create(1000);
        EXPECT_TRUE(ppgtt->Alloc(buffer[0]->size(), 0, &addr[0]));

        buffer[1] = magma::PlatformBuffer::Create(10000);
        EXPECT_TRUE(ppgtt->Alloc(buffer[1]->size(), 0, &addr[1]));

        // Try to insert without pinning
        EXPECT_FALSE(ppgtt->Insert(addr[0], buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE));

        EXPECT_TRUE(buffer[0]->PinPages(0, buffer[0]->size() / PAGE_SIZE));
        EXPECT_TRUE(buffer[1]->PinPages(0, buffer[1]->size() / PAGE_SIZE));

        // Mismatch addr and buffer
        EXPECT_FALSE(ppgtt->Insert(addr[1], buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE));

        // Totally bogus addr
        EXPECT_FALSE(
            ppgtt->Insert(0xdead1000, buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE));

        // Correct
        EXPECT_TRUE(ppgtt->Insert(addr[0], buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE));

        check_pte_entries(ppgtt.get(), buffer[0].get(), addr[0], scratch_bus_addr, CACHING_NONE);

        // Also correct
        EXPECT_TRUE(ppgtt->Insert(addr[1], buffer[1].get(), 0, buffer[1]->size(), CACHING_NONE));

        check_pte_entries(ppgtt.get(), buffer[1].get(), addr[1], scratch_bus_addr, CACHING_NONE);

        // Bogus addr
        EXPECT_FALSE(ppgtt->Clear(0xdead1000));

        // Cool
        EXPECT_TRUE(ppgtt->Clear(addr[1]));

        check_pte_entries_clear(ppgtt.get(), addr[1], buffer[1]->size(), scratch_bus_addr);

        EXPECT_TRUE(ppgtt->Clear(addr[0]));

        check_pte_entries_clear(ppgtt.get(), addr[0], buffer[0]->size(), scratch_bus_addr);

        // Bogus addr
        EXPECT_FALSE(ppgtt->Free(0xdead1000));

        // Cool
        EXPECT_TRUE(ppgtt->Free(addr[0]));
        EXPECT_TRUE(ppgtt->Free(addr[1]));

        EXPECT_TRUE(scratch_buffer->UnmapPageRangeBus(0, 1));
    }

    static void PrivatePat()
    {
        auto reg_io =
            std::unique_ptr<RegisterIo>(new RegisterIo(MockMmio::Create(8ULL * 1024 * 1024)));

        PerProcessGtt::InitPrivatePat(reg_io.get());

        EXPECT_EQ(0xA0907u, reg_io->Read32(registers::PatIndex::kOffsetLow));
        EXPECT_EQ(0x3B2B1B0Bu, reg_io->Read32(registers::PatIndex::kOffsetHigh));
    }
};

TEST(PerProcessGtt, Init) { TestPerProcessGtt::Init(); }

TEST(PerProcessGtt, Insert) { TestPerProcessGtt::Insert(); }

TEST(PerProcessGtt, PrivatePat) { TestPerProcessGtt::PrivatePat(); }
