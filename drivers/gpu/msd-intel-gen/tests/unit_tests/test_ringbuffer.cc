// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_address_space.h"
#include "ringbuffer.h"
#include "gtest/gtest.h"

class TestRingbuffer {
public:
    void CreateAndDestroy()
    {
        uint32_t size = PAGE_SIZE;
        std::unique_ptr<Ringbuffer> ringbuffer(new Ringbuffer(MsdIntelBuffer::Create(size)));
        ASSERT_NE(ringbuffer, nullptr);
        EXPECT_EQ(ringbuffer->size(), size);
    }

    void Write()
    {
        uint32_t size = PAGE_SIZE;
        std::unique_ptr<Ringbuffer> ringbuffer(new Ringbuffer(MsdIntelBuffer::Create(size)));
        ASSERT_NE(ringbuffer, nullptr);
        EXPECT_EQ(ringbuffer->size(), size);

        // Can't store full size because head==tail means empty
        EXPECT_FALSE(ringbuffer->HasSpace(size));
        EXPECT_TRUE(ringbuffer->HasSpace(size - 4));

        auto address_space = std::shared_ptr<AddressSpace>(new MockAddressSpace(0x10000, size));
        EXPECT_TRUE(ringbuffer->Map(address_space));

        uint32_t* vaddr = ringbuffer->vaddr();
        ASSERT_NE(vaddr, nullptr);

        uint32_t start_index = ringbuffer->tail() / 4;
        uint32_t size_dwords = size / 4;

        // Stuff the ringbuffer - fill to one less
        for (unsigned int i = 0; i < size_dwords - 1; i++) {
            EXPECT_TRUE(ringbuffer->HasSpace(4));
            ringbuffer->write_tail(i);
            EXPECT_EQ(vaddr[(start_index + i) % size_dwords], i);
        }

        ringbuffer->update_head(ringbuffer->tail());

        // Do it again
        for (unsigned int i = 0; i < size_dwords - 1; i++) {
            EXPECT_TRUE(ringbuffer->HasSpace(4));
            ringbuffer->write_tail(i);
            EXPECT_EQ(vaddr[(start_index + i) % size_dwords], i);
        }

        EXPECT_TRUE(ringbuffer->Unmap());
    }
};

TEST(Ringbuffer, CreateAndDestroy)
{
    TestRingbuffer test;
    test.CreateAndDestroy();
}

TEST(Ringbuffer, Write)
{
    TestRingbuffer test;
    test.Write();
}
