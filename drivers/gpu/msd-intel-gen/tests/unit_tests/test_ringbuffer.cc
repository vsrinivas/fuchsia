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

        EXPECT_TRUE(ringbuffer->HasSpace(size));
        EXPECT_TRUE(ringbuffer->HasSpace(size - 1));

        auto address_space = std::unique_ptr<AddressSpace>(new MockAddressSpace(0x10000, size));
        EXPECT_TRUE(ringbuffer->Map(address_space.get()));

        uint32_t* vaddr = ringbuffer->vaddr();
        ASSERT_NE(vaddr, nullptr);

        uint32_t dword = 0xdeadcafe;
        ringbuffer->write_tail(dword);
        EXPECT_EQ(vaddr[0], dword);

        // Stuff the ringbuffer
        uint32_t max_index = (size >> 2) - 1;
        for (unsigned int i = 1; i < max_index; i++) {
            EXPECT_TRUE(ringbuffer->HasSpace(4));
            ringbuffer->write_tail(i);
            EXPECT_EQ(vaddr[i], i);
        }

        ringbuffer->update_head(ringbuffer->tail());

        // Write the last dword
        dword = 0xabcddead;
        EXPECT_TRUE(ringbuffer->HasSpace(4));
        ringbuffer->write_tail(dword);
        EXPECT_EQ(vaddr[max_index], dword);

        // Should have wrapped.
        EXPECT_EQ(ringbuffer->tail(), 0ul);
        dword = ~0;
        EXPECT_TRUE(ringbuffer->HasSpace(4));
        ringbuffer->write_tail(dword);
        EXPECT_EQ(vaddr[0], dword);

        EXPECT_TRUE(ringbuffer->Unmap(address_space.get()));
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
