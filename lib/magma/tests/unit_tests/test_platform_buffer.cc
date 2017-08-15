// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_buffer.h"
#include "gtest/gtest.h"
#include <vector>

class TestPlatformBuffer {
public:
    static void Basic(uint64_t size)
    {
        std::unique_ptr<magma::PlatformBuffer> buffer = magma::PlatformBuffer::Create(size, "test");
        if (size == 0) {
            EXPECT_EQ(buffer, nullptr);
            return;
        }

        EXPECT_NE(buffer, nullptr);
        EXPECT_GE(buffer->size(), size);

        void* virt_addr = nullptr;
        EXPECT_TRUE(buffer->MapCpu(&virt_addr));
        EXPECT_NE(virt_addr, nullptr);

        // write first word
        static const uint32_t first_word = 0xdeadbeef;
        static const uint32_t last_word = 0x12345678;
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr)) = first_word;
        // write last word
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) + buffer->size() - 4) =
            last_word;

        uint32_t num_pages = buffer->size() / PAGE_SIZE;

        EXPECT_TRUE(buffer->UnmapCpu());

        // remap and check
        EXPECT_TRUE(buffer->MapCpu(&virt_addr));
        EXPECT_EQ(first_word, *reinterpret_cast<uint32_t*>(virt_addr));
        EXPECT_EQ(last_word, *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) +
                                                          buffer->size() - 4));
        EXPECT_TRUE(buffer->UnmapCpu());

        // pin, unpin and check again
        EXPECT_TRUE(buffer->PinPages(0, num_pages));
        EXPECT_TRUE(buffer->UnpinPages(0, num_pages));
        EXPECT_TRUE(buffer->MapCpu(&virt_addr));
        EXPECT_EQ(first_word, *reinterpret_cast<uint32_t*>(virt_addr));
        EXPECT_EQ(last_word, *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) +
                                                          buffer->size() - 4));
        EXPECT_TRUE(buffer->UnmapCpu());

        // now go page-by-page...
        EXPECT_TRUE(buffer->PinPages(0, num_pages));
        EXPECT_TRUE(buffer->MapPageCpu(0, &virt_addr));
        EXPECT_EQ(first_word, *reinterpret_cast<uint32_t*>(virt_addr));

        // pin again
        EXPECT_TRUE(buffer->PinPages(0, num_pages));
        EXPECT_TRUE(buffer->MapPageCpu(num_pages - 1, &virt_addr));
        EXPECT_EQ(last_word, *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) +
                                                          PAGE_SIZE - 4));

        // unpin once
        EXPECT_TRUE(buffer->UnpinPages(0, num_pages));

        uint64_t bus_addr;
        EXPECT_TRUE(buffer->MapPageRangeBus(0, 1, &bus_addr));
        EXPECT_TRUE(buffer->MapPageRangeBus(num_pages - 1, 1, &bus_addr));

        EXPECT_TRUE(buffer->UnmapPageCpu(0));
        EXPECT_TRUE(buffer->UnmapPageRangeBus(0, 1));

        if (size > PAGE_SIZE) {
            EXPECT_TRUE(buffer->UnmapPageCpu(num_pages - 1));
            EXPECT_TRUE(buffer->UnmapPageRangeBus(num_pages - 1, 1));
        }

        // unpin last
        EXPECT_TRUE(buffer->UnpinPages(0, num_pages));
    }

    static void test_buffer_passing(magma::PlatformBuffer* buf, magma::PlatformBuffer* buf1)
    {
        EXPECT_EQ(buf1->size(), buf->size());
        EXPECT_EQ(buf1->id(), buf->id());

        std::vector<void*> virt_addr(2);
        EXPECT_TRUE(buf1->MapCpu(&virt_addr[0]));
        EXPECT_TRUE(buf->MapCpu(&virt_addr[1]));

        unsigned int some_offset = buf->size() / 2;
        int old_value =
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[0]) + some_offset);
        int check =
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[1]) + some_offset);
        EXPECT_EQ(old_value, check);

        int new_value = old_value + 1;
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[0]) + some_offset) =
            new_value;
        check =
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[1]) + some_offset);
        EXPECT_EQ(new_value, check);

        EXPECT_TRUE(buf->UnmapCpu());
    }

    static void BufferPassing()
    {
        std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

        buffer[0] = magma::PlatformBuffer::Create(1, "test");
        ASSERT_NE(buffer[0], nullptr);
        uint32_t duplicate_handle;
        ASSERT_TRUE(buffer[0]->duplicate_handle(&duplicate_handle));
        buffer[1] = magma::PlatformBuffer::Import(duplicate_handle);
        ASSERT_NE(buffer[1], nullptr);

        EXPECT_EQ(buffer[0]->size(), buffer[1]->size());

        test_buffer_passing(buffer[0].get(), buffer[1].get());

        buffer[0] = std::move(buffer[1]);
        ASSERT_NE(buffer[0], nullptr);
        ASSERT_TRUE(buffer[0]->duplicate_handle(&duplicate_handle));
        buffer[1] = magma::PlatformBuffer::Import(duplicate_handle);
        ASSERT_NE(buffer[1], nullptr);

        EXPECT_EQ(buffer[0]->size(), buffer[1]->size());

        test_buffer_passing(buffer[0].get(), buffer[1].get());
    }

    static void BufferFdPassing()
    {
        std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

        buffer[0] = magma::PlatformBuffer::Create(1, "test");
        ASSERT_NE(buffer[0], nullptr);
        int fd;
        ASSERT_TRUE(buffer[0]->GetFd(&fd));
        buffer[1] = magma::PlatformBuffer::ImportFromFd(fd);
        ASSERT_NE(buffer[1], nullptr);

        EXPECT_EQ(buffer[0]->size(), buffer[1]->size());
        EXPECT_EQ(0, close(fd));

        test_buffer_passing(buffer[0].get(), buffer[1].get());

        buffer[0] = std::move(buffer[1]);
        ASSERT_NE(buffer[0], nullptr);
        ASSERT_TRUE(buffer[0]->GetFd(&fd));
        buffer[1] = magma::PlatformBuffer::ImportFromFd(fd);
        ASSERT_NE(buffer[1], nullptr);

        EXPECT_EQ(buffer[0]->size(), buffer[1]->size());

        test_buffer_passing(buffer[0].get(), buffer[1].get());
        EXPECT_EQ(0, close(fd));
    }

    static void PinRanges(uint32_t num_pages)
    {
        std::unique_ptr<magma::PlatformBuffer> buffer =
            magma::PlatformBuffer::Create(num_pages * PAGE_SIZE, "test");

        for (uint32_t i = 0; i < num_pages; i++) {
            uint64_t phys_addr = 0;
            EXPECT_FALSE(buffer->MapPageRangeBus(i, 1, &phys_addr));
        }

        EXPECT_FALSE(buffer->UnpinPages(0, num_pages));

        EXPECT_TRUE(buffer->PinPages(0, num_pages));

        for (uint32_t i = 0; i < num_pages; i++) {
            uint64_t phys_addr = 0;
            EXPECT_TRUE(buffer->MapPageRangeBus(i, 1, &phys_addr));
            EXPECT_NE(phys_addr, 0u);
        }

        // Map first page again
        EXPECT_TRUE(buffer->PinPages(0, 1));

        // Unpin full range
        EXPECT_TRUE(buffer->UnpinPages(0, num_pages));

        for (uint32_t i = 0; i < num_pages; i++) {
            uint64_t phys_addr = 0;
            if (i == 0) {
                EXPECT_TRUE(buffer->MapPageRangeBus(i, 1, &phys_addr));
                EXPECT_TRUE(buffer->UnmapPageRangeBus(i, 1));
            } else
                EXPECT_FALSE(buffer->MapPageRangeBus(i, 1, &phys_addr));
        }

        EXPECT_FALSE(buffer->UnpinPages(0, num_pages));
        EXPECT_TRUE(buffer->UnpinPages(0, 1));

        // Map the middle page.
        EXPECT_TRUE(buffer->PinPages(num_pages / 2, 1));

        // Map a middle range.
        uint32_t range_start = num_pages / 2 - 1;
        uint32_t range_pages = 3;
        ASSERT_GE(num_pages, range_pages);

        EXPECT_TRUE(buffer->PinPages(range_start, range_pages));

        // Verify middle range is mapped.
        for (uint32_t i = 0; i < num_pages; i++) {
            uint64_t phys_addr = 0;
            if (i >= range_start && i < range_start + range_pages) {
                EXPECT_TRUE(buffer->MapPageRangeBus(i, 1, &phys_addr));
                EXPECT_TRUE(buffer->UnmapPageRangeBus(i, 1));
            } else
                EXPECT_FALSE(buffer->MapPageRangeBus(i, 1, &phys_addr));
        }

        // Unpin middle page.
        EXPECT_TRUE(buffer->UnpinPages(num_pages / 2, 1));

        // Same result.
        for (uint32_t i = 0; i < num_pages; i++) {
            uint64_t phys_addr = 0;
            if (i >= range_start && i < range_start + range_pages) {
                EXPECT_TRUE(buffer->MapPageRangeBus(i, 1, &phys_addr));
                EXPECT_TRUE(buffer->UnmapPageRangeBus(i, 1));
            } else
                EXPECT_FALSE(buffer->MapPageRangeBus(i, 1, &phys_addr));
        }

        EXPECT_TRUE(buffer->UnpinPages(range_start, range_pages));

        for (uint32_t i = 0; i < num_pages; i++) {
            uint64_t phys_addr = 0;
            EXPECT_FALSE(buffer->MapPageRangeBus(i, 1, &phys_addr));
        }
    }

    static void CommitPages(uint32_t num_pages)
    {
        std::unique_ptr<magma::PlatformBuffer> buffer =
            magma::PlatformBuffer::Create(num_pages * PAGE_SIZE, "test");

        // start of range invalid
        EXPECT_FALSE(buffer->CommitPages(num_pages, 1));
        // end of range invalid
        EXPECT_FALSE(buffer->CommitPages(0, num_pages + 1));
        // one page in the middle
        EXPECT_TRUE(buffer->CommitPages(num_pages / 2, 1));
        // entire buffer
        EXPECT_TRUE(buffer->CommitPages(0, num_pages));
        // entire buffer again
        EXPECT_TRUE(buffer->CommitPages(0, num_pages));
    }
};

TEST(PlatformBuffer, Basic)
{
    TestPlatformBuffer::Basic(0);
    TestPlatformBuffer::Basic(1);
    TestPlatformBuffer::Basic(4095);
    TestPlatformBuffer::Basic(4096);
    TestPlatformBuffer::Basic(4097);
    TestPlatformBuffer::Basic(20 * PAGE_SIZE);
    TestPlatformBuffer::Basic(10 * 1024 * 1024);
}

TEST(PlatformBuffer, BufferPassing) { TestPlatformBuffer::BufferPassing(); }
TEST(PlatformBuffer, BufferFdPassing) { TestPlatformBuffer::BufferFdPassing(); }

TEST(PlatformBuffer, Commit)
{
    TestPlatformBuffer::CommitPages(1);
    TestPlatformBuffer::CommitPages(16);
    TestPlatformBuffer::CommitPages(1024);
}
