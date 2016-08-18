// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_address_space.h"
#include "msd_intel_buffer.h"
#include "gtest/gtest.h"

TEST(MsdIntelBuffer, CreateAndDestroy)
{
    std::unique_ptr<MsdIntelBuffer> buffer;
    uint64_t size;

    buffer = MsdIntelBuffer::Create(size = 0);
    EXPECT_EQ(buffer, nullptr);

    buffer = MsdIntelBuffer::Create(size = 100);
    EXPECT_NE(buffer, nullptr);
    EXPECT_GE(buffer->platform_buffer()->size(), size);

    buffer = MsdIntelBuffer::Create(size = 10000);
    EXPECT_NE(buffer, nullptr);
    EXPECT_GE(buffer->platform_buffer()->size(), size);
}

TEST(MsdIntelBuffer, MapGpu)
{
    uint64_t base = 0x10000;
    uint64_t size = PAGE_SIZE * 10;

    std::unique_ptr<MockAddressSpace> address_space(new MockAddressSpace(base, size));

    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE));
    ASSERT_NE(buffer, nullptr);

    bool ret = buffer->MapGpu(address_space.get(), 0);
    EXPECT_TRUE(ret);

    gpu_addr_t gpu_addr;
    ret = buffer->GetGpuAddress(address_space->id(), &gpu_addr);
    EXPECT_TRUE(ret);

    EXPECT_EQ(gpu_addr, base);
    EXPECT_TRUE(address_space->is_allocated(gpu_addr));
    EXPECT_FALSE(address_space->is_clear(gpu_addr));

    ret = buffer->UnmapGpu(address_space.get());
    EXPECT_TRUE(ret);

    EXPECT_FALSE(address_space->is_allocated(gpu_addr));
    EXPECT_TRUE(address_space->is_clear(gpu_addr));
}
