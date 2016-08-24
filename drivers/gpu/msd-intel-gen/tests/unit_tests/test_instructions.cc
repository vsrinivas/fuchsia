// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_address_space.h"
#include "instructions.h"
#include "gtest/gtest.h"

class TestRingbuffer {
public:
    static uint32_t* vaddr(Ringbuffer* ringbuffer) { return ringbuffer->vaddr(); }
};

class TestInstructions {
public:
    TestInstructions()
    {
        ringbuffer_ = std::unique_ptr<Ringbuffer>(new Ringbuffer(MsdIntelBuffer::Create(PAGE_SIZE)));
        address_space_ = std::unique_ptr<MockAddressSpace>(new MockAddressSpace(0x10000, ringbuffer_->size()));   

        EXPECT_TRUE(ringbuffer_->Map(address_space_.get()));
    }

    void Noop()
    {
        uint32_t* vaddr = TestRingbuffer::vaddr(ringbuffer_.get());
        
        MiNoop::write_ringbuffer(ringbuffer_.get());

        EXPECT_EQ(*vaddr, 0u);
    }

    void BatchBufferStart()
    {          
        ASSERT_EQ((int)MiBatchBufferStart::kDwordCount, 3);

        uint32_t* vaddr = TestRingbuffer::vaddr(ringbuffer_.get());

        gpu_addr_t gpu_addr = 0xabcd1234cafebeef;
        MiBatchBufferStart::write_ringbuffer(ringbuffer_.get(), gpu_addr, true);
        
        EXPECT_EQ(ringbuffer_->tail(), MiBatchBufferStart::kDwordCount * sizeof(uint32_t));
        EXPECT_EQ(*vaddr++, MiBatchBufferStart::kCommandType | MiBatchBufferStart::kAddressSpacePpgtt);
        EXPECT_EQ(*vaddr++, magma::lower_32_bits(gpu_addr));
        EXPECT_EQ(*vaddr++, magma::upper_32_bits(gpu_addr));

        gpu_addr = 0xaa00bb00cc00dd;
        MiBatchBufferStart::write_ringbuffer(ringbuffer_.get(), gpu_addr, false);
        EXPECT_EQ(ringbuffer_->tail(), 2 * MiBatchBufferStart::kDwordCount * sizeof(uint32_t));
        EXPECT_EQ(*vaddr++, (uint32_t)MiBatchBufferStart::kCommandType);
        EXPECT_EQ(*vaddr++, magma::lower_32_bits(gpu_addr));
        EXPECT_EQ(*vaddr++, magma::upper_32_bits(gpu_addr));
    }

    void StoreDataImmediate()
    {
        ASSERT_EQ((int)MiStoreDataImmediate::kDwordCount, 4);

        uint32_t* vaddr = TestRingbuffer::vaddr(ringbuffer_.get());

        gpu_addr_t gpu_addr = 0xabcd1234cafebeef;
        uint32_t val = ~0;

        MiStoreDataImmediate::write_ringbuffer(ringbuffer_.get(), val, gpu_addr, true);
        EXPECT_EQ(ringbuffer_->tail(), MiStoreDataImmediate::kDwordCount * sizeof(uint32_t));
        EXPECT_EQ(*vaddr++, MiStoreDataImmediate::kCommandType | (MiStoreDataImmediate::kDwordCount - 2) | MiStoreDataImmediate::kAddressSpaceGtt);
        EXPECT_EQ(*vaddr++, magma::lower_32_bits(gpu_addr));
        EXPECT_EQ(*vaddr++, magma::upper_32_bits(gpu_addr));
        EXPECT_EQ(*vaddr++, val);

        gpu_addr = 0xaa00bb00cc00dd;
        val = 0xdadacdcd;

        MiStoreDataImmediate::write_ringbuffer(ringbuffer_.get(), val, gpu_addr, false);
        EXPECT_EQ(ringbuffer_->tail(), 2 * MiStoreDataImmediate::kDwordCount * sizeof(uint32_t));
        EXPECT_EQ(*vaddr++, MiStoreDataImmediate::kCommandType | (MiStoreDataImmediate::kDwordCount - 2));
        EXPECT_EQ(*vaddr++, magma::lower_32_bits(gpu_addr));
        EXPECT_EQ(*vaddr++, magma::upper_32_bits(gpu_addr));
        EXPECT_EQ(*vaddr++, val);        
    }

private:
    std::unique_ptr<Ringbuffer> ringbuffer_;
    std::unique_ptr<AddressSpace> address_space_;
};

TEST(Instructions, Noop)
{
    TestInstructions test;
    test.Noop();    
}

TEST(Instructions, BatchBufferStart)
{
    TestInstructions test;
    test.BatchBufferStart();   
}

TEST(Instructions, StoreDataImmediate)
{
    TestInstructions test;
    test.StoreDataImmediate();   
}
