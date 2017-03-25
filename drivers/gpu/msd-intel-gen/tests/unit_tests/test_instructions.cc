// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "instructions.h"
#include "mock/mock_address_space.h"
#include "ringbuffer.h"
#include "gtest/gtest.h"

class TestRingbuffer {
public:
    static uint32_t* vaddr(Ringbuffer* ringbuffer) { return ringbuffer->vaddr(); }
};

class TestInstructions {
public:
    TestInstructions()
    {
        ringbuffer_ =
            std::unique_ptr<Ringbuffer>(new Ringbuffer(MsdIntelBuffer::Create(PAGE_SIZE)));
        address_space_ =
            std::shared_ptr<MockAddressSpace>(new MockAddressSpace(0x10000, ringbuffer_->size()));

        EXPECT_TRUE(ringbuffer_->Map(address_space_));
    }

    void Noop()
    {
        uint32_t* vaddr = TestRingbuffer::vaddr(ringbuffer_.get());

        MiNoop::write(ringbuffer_.get());

        EXPECT_EQ(*vaddr, 0u);
    }

    void BatchBufferStart()
    {
        ASSERT_EQ((int)MiBatchBufferStart::kDwordCount, 3);

        uint32_t tail_start = ringbuffer_->tail();

        uint32_t* vaddr = TestRingbuffer::vaddr(ringbuffer_.get()) + tail_start / 4;

        gpu_addr_t gpu_addr = 0xabcd1234cafebeef;
        MiBatchBufferStart::write(ringbuffer_.get(), gpu_addr, ADDRESS_SPACE_PPGTT);

        EXPECT_EQ(ringbuffer_->tail() - tail_start,
                  MiBatchBufferStart::kDwordCount * sizeof(uint32_t));
        EXPECT_EQ(*vaddr++, MiBatchBufferStart::kCommandType |
                                (MiBatchBufferStart::kDwordCount - 2) |
                                MiBatchBufferStart::kAddressSpacePpgtt);
        EXPECT_EQ(*vaddr++, magma::lower_32_bits(gpu_addr));
        EXPECT_EQ(*vaddr++, magma::upper_32_bits(gpu_addr));

        gpu_addr = 0xaa00bb00cc00dd;

        MiBatchBufferStart::write(ringbuffer_.get(), gpu_addr, ADDRESS_SPACE_GGTT);
        EXPECT_EQ(ringbuffer_->tail() - tail_start,
                  2 * MiBatchBufferStart::kDwordCount * sizeof(uint32_t));
        EXPECT_EQ(*vaddr++,
                  MiBatchBufferStart::kCommandType | (MiBatchBufferStart::kDwordCount - 2));
        EXPECT_EQ(*vaddr++, magma::lower_32_bits(gpu_addr));
        EXPECT_EQ(*vaddr++, magma::upper_32_bits(gpu_addr));
    }

    void PipeControl()
    {
        ASSERT_EQ((int)MiPipeControl::kDwordCount, 6);

        uint32_t tail_start = ringbuffer_->tail();

        uint32_t* vaddr = TestRingbuffer::vaddr(ringbuffer_.get()) + tail_start / 4;

        gpu_addr_t gpu_addr = 0xabcd1234cafebeef;

        uint32_t sequence_number = 0xdeadbeef;
        uint32_t flags = MiPipeControl::kCommandStreamerStallEnableBit |
                         MiPipeControl::kIndirectStatePointersDisableBit |
                         MiPipeControl::kGenericMediaStateClearBit |
                         MiPipeControl::kDcFlushEnableBit;

        MiPipeControl::write(ringbuffer_.get(), sequence_number, gpu_addr, flags);

        EXPECT_EQ(ringbuffer_->tail() - tail_start, MiPipeControl::kDwordCount * sizeof(uint32_t));
        EXPECT_EQ(0x7A000000u | (MiPipeControl::kDwordCount - 2), *vaddr++);
        EXPECT_EQ(flags | MiPipeControl::kPostSyncWriteImmediateBit |
                      MiPipeControl::kAddressSpaceGlobalGttBit,
                  *vaddr++);
        EXPECT_EQ(magma::lower_32_bits(gpu_addr), *vaddr++);
        EXPECT_EQ(magma::upper_32_bits(gpu_addr), *vaddr++);
        EXPECT_EQ(sequence_number, *vaddr++);
        EXPECT_EQ(0u, *vaddr++);
    }

private:
    // order of destruction important so gpu mappings can access the address space
    std::shared_ptr<AddressSpace> address_space_;
    std::unique_ptr<Ringbuffer> ringbuffer_;
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

TEST(Instructions, PipeControl)
{
    TestInstructions test;
    test.PipeControl();
}
