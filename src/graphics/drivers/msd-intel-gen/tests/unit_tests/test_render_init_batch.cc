// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>

#include "address_space.h"
#include "render_init_batch.h"

using AllocatingAddressSpace = FakeAllocatingAddressSpace<GpuMapping, AddressSpace>;

class TestRenderInitBatch {
 public:
  class AddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    virtual ~AddressSpaceOwner() = default;
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  void Init(std::unique_ptr<RenderInitBatch> batch) {
    auto owner = std::make_unique<AddressSpaceOwner>();

    uint64_t base = 0x10000;
    auto address_space = std::make_shared<AllocatingAddressSpace>(
        owner.get(), base, magma::round_up(batch->size(), PAGE_SIZE));

    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(batch->size(), "test"));
    ASSERT_NE(buffer, nullptr);

    void* addr;
    ASSERT_TRUE(buffer->platform_buffer()->MapCpu(&addr));

    // Set buffer to a known pattern
    memset(addr, 0xFF, buffer->platform_buffer()->size());

    EXPECT_TRUE(buffer->platform_buffer()->UnmapCpu());

    auto mapping = batch->Init(std::move(buffer), address_space);
    ASSERT_NE(mapping, nullptr);

    gpu_addr_t gpu_addr = mapping->gpu_addr();

    ASSERT_TRUE(mapping->buffer()->platform_buffer()->MapCpu(&addr));

    auto entry = reinterpret_cast<uint32_t*>(addr);

    // Check relocations
    for (unsigned int i = 0; i < batch->relocation_count_; i++) {
      uint32_t index = batch->relocs_[i] >> 2;
      uint64_t val = entry[index + 1];
      val = (val << 32) | entry[index];
      EXPECT_EQ(val, gpu_addr + batch->batch_[index]);
      // Remove reloc for following memcmp
      entry[index] = batch->batch_[index];
      entry[index + 1] = batch->batch_[index + 1];
    }

    // Check everything else
    EXPECT_EQ(memcmp(addr, batch->batch_, batch->size()), 0);

    EXPECT_TRUE(mapping->buffer()->platform_buffer()->UnmapCpu());
  }
};

TEST(RenderInitBatch, Init) {
  TestRenderInitBatch().Init(std::unique_ptr<RenderInitBatch>(new RenderInitBatchGen9()));
}
