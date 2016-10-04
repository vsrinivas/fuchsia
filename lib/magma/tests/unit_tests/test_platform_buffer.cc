// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/platform/platform_buffer.h"
#include "gtest/gtest.h"
#include <vector>

static void TestPlatformBuffer(uint64_t size)
{
    std::unique_ptr<magma::PlatformBuffer> buffer = magma::PlatformBuffer::Create(size);
    if (size == 0) {
        EXPECT_EQ(buffer, nullptr);
        return;
    }

    EXPECT_NE(buffer, nullptr);
    EXPECT_GE(buffer->size(), size);

    void* virt_addr = nullptr;
    bool ret = buffer->MapCpu(&virt_addr);
    EXPECT_EQ(ret, true);
    EXPECT_NE(virt_addr, nullptr);

    // write first word
    static const uint32_t first_word = 0xdeadbeef;
    static const uint32_t last_word = 0x12345678;
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr)) = first_word;
    // write last word
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) + buffer->size() - 4) =
        last_word;

    ret = buffer->UnmapCpu();
    EXPECT_EQ(ret, true);

    unsigned int num_pages;
    unsigned int pinned_page_count;

    ret = buffer->PinnedPageCount(&pinned_page_count);
    EXPECT_EQ(ret, false);

    ret = buffer->PinPages(&num_pages);
    EXPECT_EQ(ret, true);
    EXPECT_GE(num_pages, buffer->size() / PAGE_SIZE);

    ret = buffer->PinnedPageCount(&pinned_page_count);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(num_pages, pinned_page_count);

    ret = buffer->MapPageCpu(0, &virt_addr);
    EXPECT_EQ(ret, true);
    uint32_t check = *reinterpret_cast<uint32_t*>(virt_addr);
    EXPECT_EQ(check, first_word);

    // pin again
    ret = buffer->PinPages();
    EXPECT_EQ(ret, true);

    ret = buffer->MapPageCpu(num_pages - 1, &virt_addr);
    EXPECT_EQ(ret, true);
    check = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr) + PAGE_SIZE - 4);
    EXPECT_EQ(check, last_word);

    // unpin once
    ret = buffer->UnpinPages();
    EXPECT_EQ(ret, true);

    uint64_t bus_addr;
    ret = buffer->MapPageBus(0, &bus_addr);
    EXPECT_EQ(ret, true);
    ret = buffer->MapPageBus(num_pages - 1, &bus_addr);
    EXPECT_EQ(ret, true);

    ret = buffer->UnmapPageCpu(0);
    EXPECT_EQ(ret, true);
    ret = buffer->UnmapPageCpu(num_pages - 1);
    EXPECT_EQ(ret, true);
    ret = buffer->UnmapPageBus(0);
    EXPECT_EQ(ret, true);
    ret = buffer->UnmapPageCpu(num_pages - 1);
    EXPECT_EQ(ret, true);

    // unpin last
    ret = buffer->UnpinPages();
    EXPECT_EQ(ret, true);
}

static void test_buffer_passing(magma::PlatformBuffer* buf, magma::PlatformBuffer* buf1)
{
    EXPECT_EQ(buf1->size(), buf->size());
    EXPECT_EQ(buf1->handle(), buf->handle());

    std::vector<void*> virt_addr(2);
    int ret = buf1->MapCpu(&virt_addr[0]);
    EXPECT_EQ(ret, true);
    ret = buf->MapCpu(&virt_addr[1]);
    EXPECT_EQ(ret, true);

    unsigned int some_offset = buf->size() / 2;
    int old_value =
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[0]) + some_offset);
    int check =
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[1]) + some_offset);
    EXPECT_EQ(old_value, check);

    int new_value = old_value + 1;
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[0]) + some_offset) =
        new_value;
    check = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(virt_addr[1]) + some_offset);
    EXPECT_EQ(new_value, check);

    ret = buf->UnmapCpu();
    EXPECT_EQ(ret, true);
}

static void TestPlatformBufferPassing()
{
    std::vector<msd_platform_buffer*> token(2);
    std::vector<std::unique_ptr<magma::PlatformBuffer>> buffer(2);

    buffer[0] = magma::PlatformBuffer::Create(1, &token[0]);
    buffer[1] = magma::PlatformBuffer::Create(token[0]);

    test_buffer_passing(buffer[0].get(), buffer[1].get());

    buffer[0] = std::move(buffer[1]);
    buffer[1] = magma::PlatformBuffer::Create(token[0]);

    test_buffer_passing(buffer[0].get(), buffer[1].get());
}

TEST(MagmaUtil, PlatformBuffer)
{
    TestPlatformBuffer(0);
    TestPlatformBuffer(1);
    TestPlatformBuffer(4095);
    TestPlatformBuffer(4096);
    TestPlatformBuffer(4097);
    TestPlatformBuffer(20 * PAGE_SIZE);
    TestPlatformBuffer(10 * 1024 * 1024);
    TestPlatformBufferPassing();
}
