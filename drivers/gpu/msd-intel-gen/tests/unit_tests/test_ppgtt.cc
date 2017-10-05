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

    static void check_pte_entries_zero(PerProcessGtt* ppgtt, uint64_t gpu_addr, uint64_t size)
    {
        uint32_t page_count = size >> PAGE_SHIFT;

        for (unsigned int i = 0; i < page_count; i++) {
            uint64_t pte = ppgtt->get_pte(gpu_addr + i * PAGE_SIZE);
            EXPECT_EQ(0u, pte);
        }
    }

    static void check_pte_entries_clear(PerProcessGtt* ppgtt, uint64_t gpu_addr, uint64_t size)
    {
        uint32_t page_count = size >> PAGE_SHIFT;

        for (unsigned int i = 0; i < page_count; i++) {
            uint64_t pte = ppgtt->get_pte(gpu_addr + i * PAGE_SIZE);
            EXPECT_EQ(pte & ~(PAGE_SIZE - 1), ppgtt->pml4_table()->scratch_page_bus_addr());
            EXPECT_TRUE(pte & (1 << 0));  // present
            EXPECT_FALSE(pte & (1 << 1)); // not writeable
            EXPECT_EQ(pte & cache_bits(CACHING_NONE), cache_bits(CACHING_NONE));
        }
    }

    static void check_pte_entries(PerProcessGtt* ppgtt, magma::PlatformBuffer* buffer,
                                  uint64_t gpu_addr, CachingType caching_type)
    {
        ASSERT_TRUE(magma::is_page_aligned(buffer->size()));
        uint32_t page_count = buffer->size() / PAGE_SIZE;

        uint64_t bus_addr[page_count];
        EXPECT_TRUE(buffer->MapPageRangeBus(0, page_count, bus_addr));

        for (unsigned int i = 0;
             i < page_count + PerProcessGtt::kOverfetchPageCount + PerProcessGtt::kGuardPageCount;
             i++) {
            uint64_t pte = ppgtt->get_pte(gpu_addr + i * PAGE_SIZE);
            if (i < page_count) {
                EXPECT_EQ(pte & ~(PAGE_SIZE - 1), bus_addr[i]);
            } else {
                EXPECT_EQ(pte & ~(PAGE_SIZE - 1), ppgtt->pml4_table()->scratch_page_bus_addr());
            }

            EXPECT_TRUE(pte & (1 << 0));
            EXPECT_EQ(static_cast<bool>(pte & (1 << 1)), i < page_count); // writeable
            EXPECT_EQ(pte & cache_bits(caching_type), cache_bits(caching_type));
        }
        EXPECT_TRUE(buffer->UnmapPageRangeBus(0, page_count));
    }

    static void Init()
    {
        auto ppgtt = PerProcessGtt::Create(GpuMappingCache::Create());
        ASSERT_TRUE(ppgtt->Init());

        check_pte_entries_zero(ppgtt.get(), (1ull << 48) - PAGE_SIZE, PAGE_SIZE);
        check_pte_entries_zero(ppgtt.get(), (1ull << 47) - PAGE_SIZE, PAGE_SIZE);
        check_pte_entries_zero(ppgtt.get(), (1ull << 40) - PAGE_SIZE, PAGE_SIZE);
        check_pte_entries_zero(ppgtt.get(), (1ull << 33) - PAGE_SIZE, PAGE_SIZE);
        check_pte_entries_zero(ppgtt.get(), (1ull << 32) - PAGE_SIZE, PAGE_SIZE);
        check_pte_entries_zero(ppgtt.get(), (1ull << 31) - PAGE_SIZE, PAGE_SIZE);
        check_pte_entries_zero(ppgtt.get(), 0, ppgtt->Size());
    }

    static void Error()
    {
        auto ppgtt = PerProcessGtt::Create(GpuMappingCache::Create());
        EXPECT_TRUE(ppgtt->Init());

        std::vector<uint64_t> addr(2);
        std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

        buffer[0] = magma::PlatformBuffer::Create(PAGE_SIZE, "test");
        EXPECT_TRUE(ppgtt->Alloc(buffer[0]->size(), 0, &addr[0]));

        buffer[1] = magma::PlatformBuffer::Create(PAGE_SIZE * 2, "test");
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

        // Bogus addr
        EXPECT_FALSE(ppgtt->Clear(0xdead1000));

        // Bogus addr
        EXPECT_FALSE(ppgtt->Free(0xdead1000));
    }

    static void Insert()
    {
        auto ppgtt = PerProcessGtt::Create(GpuMappingCache::Create());
        EXPECT_TRUE(ppgtt->Init());

        std::vector<uint64_t> addr(2);
        std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

        // Placeholder occupies most of the first page directory
        uint64_t placeholder_addr;
        auto placeholder = magma::PlatformBuffer::Create(512 * 511 * PAGE_SIZE, "placeholder");
        EXPECT_TRUE(ppgtt->Alloc(placeholder->size(), 0, &placeholder_addr));

        buffer[0] = magma::PlatformBuffer::Create(513 * PAGE_SIZE, "test");
        EXPECT_TRUE(ppgtt->Alloc(buffer[0]->size(), 0, &addr[0]));

        buffer[1] = magma::PlatformBuffer::Create(10000, "test");
        EXPECT_TRUE(ppgtt->Alloc(buffer[1]->size(), 0, &addr[1]));

        EXPECT_TRUE(buffer[0]->PinPages(0, buffer[0]->size() / PAGE_SIZE));
        EXPECT_TRUE(buffer[1]->PinPages(0, buffer[1]->size() / PAGE_SIZE));

        EXPECT_TRUE(ppgtt->Insert(addr[0], buffer[0].get(), 0, buffer[0]->size(), CACHING_NONE));
        check_pte_entries(ppgtt.get(), buffer[0].get(), addr[0], CACHING_NONE);

        EXPECT_TRUE(ppgtt->Insert(addr[1], buffer[1].get(), 0, buffer[1]->size(), CACHING_NONE));
        check_pte_entries(ppgtt.get(), buffer[1].get(), addr[1], CACHING_NONE);

        EXPECT_TRUE(ppgtt->Clear(addr[1]));
        check_pte_entries_clear(ppgtt.get(), addr[1], buffer[1]->size());

        EXPECT_TRUE(ppgtt->Clear(addr[0]));
        check_pte_entries_clear(ppgtt.get(), addr[0], buffer[0]->size());

        EXPECT_TRUE(ppgtt->Free(addr[0]));
        EXPECT_TRUE(ppgtt->Free(addr[1]));
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

TEST(PerProcessGtt, Error) { TestPerProcessGtt::Error(); }

TEST(PerProcessGtt, Insert) { TestPerProcessGtt::Insert(); }

TEST(PerProcessGtt, PrivatePat) { TestPerProcessGtt::PrivatePat(); }
