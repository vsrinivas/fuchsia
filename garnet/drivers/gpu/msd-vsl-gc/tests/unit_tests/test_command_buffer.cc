// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/command_buffer.h"
#include "garnet/drivers/gpu/msd-vsl-gc/src/instructions.h"
#include "garnet/drivers/gpu/msd-vsl-gc/src/msd_vsl_device.h"
#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"

class TestCommandBuffer : public ::testing::Test {
 public:
  static constexpr uint32_t kAddressSpaceIndex = 1;

  void SetUp() override {
    device_ = MsdVslDevice::Create(GetTestDeviceHandle(), true /* start_device_thread */);
    ASSERT_NE(device_, nullptr);
    ASSERT_TRUE(device_->IsIdle());

    address_space_owner_ = std::make_unique<AddressSpaceOwner>(device_->GetBusMapper());
    address_space_ = AddressSpace::Create(address_space_owner_.get(), kAddressSpaceIndex);
    ASSERT_NE(address_space_, nullptr);

    device_->page_table_arrays()->AssignAddressSpace(kAddressSpaceIndex, address_space_.get());

    std::weak_ptr<MsdVslConnection> connection;
    context_ = std::make_shared<MsdVslContext>(connection, address_space_);
    ASSERT_NE(context_, nullptr);
  }

 protected:
  class AddressSpaceOwner : public AddressSpace::Owner {
   public:
    AddressSpaceOwner(magma::PlatformBusMapper* bus_mapper) : bus_mapper_(bus_mapper) {}
    virtual ~AddressSpaceOwner() = default;

    void AddressSpaceReleased(AddressSpace* address_space) override {}

    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_; }

   private:
    magma::PlatformBusMapper* bus_mapper_;
  };

  // Creates a buffer of |buffer_size| bytes, and maps the buffer to |gpu_addr|.
  // |map_page_count| may be less bytes than buffer size.
  void CreateAndMapBuffer(uint32_t buffer_size, uint32_t map_page_count, uint32_t gpu_addr,
                          std::shared_ptr<MsdVslBuffer>* out_buffer);

  // Creates a new command buffer.
  // |data_size| is the actual length of the user provided data and may be smaller than
  // the size of |buffer|.
  // |signal| is an optional semaphore. If present, it will be signalled after the batch
  // is submitted via |SubmitBatch| and execution completes.
  void CreateAndPrepareBatch(std::shared_ptr<MsdVslBuffer> buffer, uint32_t data_size,
                             uint32_t batch_offset,
                             std::shared_ptr<magma::PlatformSemaphore> signal,
                             std::unique_ptr<CommandBuffer>* out_batch);

  std::unique_ptr<MsdVslDevice> device_;
  std::shared_ptr<MsdVslContext> context_;
  std::unique_ptr<AddressSpaceOwner> address_space_owner_;
  std::shared_ptr<AddressSpace> address_space_;
};

void TestCommandBuffer::CreateAndMapBuffer(uint32_t buffer_size, uint32_t map_page_count,
                                           uint32_t gpu_addr,
                                           std::shared_ptr<MsdVslBuffer>* out_buffer) {
  std::unique_ptr<magma::PlatformBuffer> buffer =
      magma::PlatformBuffer::Create(buffer_size, "test buffer");
  ASSERT_NE(buffer, nullptr);

  ASSERT_TRUE(buffer->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED));

  auto msd_buffer = std::make_shared<MsdVslBuffer>(std::move(buffer));
  ASSERT_NE(msd_buffer, nullptr);

  std::shared_ptr<GpuMapping> gpu_mapping;
  magma::Status status = AddressSpace::MapBufferGpu(address_space_,
     msd_buffer, gpu_addr, 0 /* page_offset */, map_page_count, &gpu_mapping);
  ASSERT_TRUE(status.ok());
  ASSERT_NE(gpu_mapping, nullptr);

  ASSERT_TRUE(address_space_->AddMapping(std::move(gpu_mapping)));

  *out_buffer = msd_buffer;
}

void TestCommandBuffer::CreateAndPrepareBatch(std::shared_ptr<MsdVslBuffer> buffer,
                                              uint32_t data_size, uint32_t batch_offset,
                                              std::shared_ptr<magma::PlatformSemaphore> signal,
                                              std::unique_ptr<CommandBuffer>* out_batch) {
  auto command_buffer = std::make_unique<magma_system_command_buffer>(magma_system_command_buffer{
    .batch_buffer_resource_index = 0,
    .batch_start_offset = batch_offset,
    .num_resources = 1,
    .wait_semaphore_count = 0,
    .signal_semaphore_count = signal ? 1 : 0u,
  });
  auto batch = std::make_unique<CommandBuffer>(context_, 0, std::move(command_buffer));
  ASSERT_NE(batch, nullptr);

  std::vector<CommandBuffer::ExecResource> resources;
  resources.emplace_back(CommandBuffer::ExecResource{
    .buffer = buffer,
    .offset = 0,
    .length = data_size
  });

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  if (signal) {
    signal_semaphores.push_back(signal);
  }
  ASSERT_TRUE(batch->InitializeResources(std::move(resources), std::move(wait_semaphores),
                                         std::move(signal_semaphores)));
  ASSERT_TRUE(batch->PrepareForExecution());
  *out_batch = std::move(batch);
}

// Tests submitting a simple batch that also provides a non-zero batch offset.
TEST_F(TestCommandBuffer, SubmitBatchWithOffset) {
  constexpr uint32_t kBufferSize = 4096;
  constexpr uint32_t kMapPageCount = 1;
  constexpr uint32_t kDataSize = 4;
  // The user data will start at a non-zero offset.
  constexpr uint32_t kBatchOffset = 80;
  constexpr uint32_t gpu_addr = 0x10000;

  std::shared_ptr<MsdVslBuffer> buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(kBufferSize, kMapPageCount, gpu_addr, &buffer));

  // Write a WAIT command at offset |kBatchOffset|.
  uint32_t* cmd_ptr;
  ASSERT_TRUE(buffer->platform_buffer()->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));
  BufferWriter buf_writer(cmd_ptr, kBufferSize, kBatchOffset);
  MiWait::write(&buf_writer);
  ASSERT_TRUE(buffer->platform_buffer()->UnmapCpu());

  // Submit the batch and verify we get a completion event.
  auto semaphore = magma::PlatformSemaphore::Create();
  EXPECT_NE(semaphore, nullptr);

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndPrepareBatch(buffer, kDataSize, kBatchOffset, semaphore->Clone(), &batch));
  ASSERT_TRUE(batch->IsValidBatchBuffer());

  ASSERT_TRUE(device_->SubmitBatch(std::move(batch)).ok());

  constexpr uint64_t kTimeoutMs = 1000;
  EXPECT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());
}

// Unit tests for |IsValidBatchBatch|.
class TestIsValidBatchBuffer : public TestCommandBuffer {
 public:
  struct BufferDesc {
    uint32_t buffer_size;
    uint32_t map_page_count;
    uint32_t data_size;
    uint32_t batch_offset;
    uint32_t gpu_addr;
  };

  void DoTest(const BufferDesc& buffer_desc, bool want_is_valid) {
    std::shared_ptr<MsdVslBuffer> buffer;
    ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(
        buffer_desc.buffer_size, buffer_desc.map_page_count, buffer_desc.gpu_addr, &buffer));

    std::unique_ptr<CommandBuffer> batch;
    ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(
        buffer, buffer_desc.data_size, buffer_desc.batch_offset, nullptr /* signal */, &batch));
    ASSERT_EQ(want_is_valid, batch->IsValidBatchBuffer());
  }
};

TEST_F(TestIsValidBatchBuffer, ValidBatch) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096,
    .map_page_count = 1,
    .data_size = 4088,  // 8 bytes remaining in buffer.
    .batch_offset = 0,
    .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, true);
}

TEST_F(TestIsValidBatchBuffer, BufferTooSmall) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096,
    .map_page_count = 1,
    .data_size = 4090,  // Only 6 bytes remaining in buffer.
    .batch_offset = 0,
    .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, false);
}

TEST_F(TestIsValidBatchBuffer, NotEnoughPagesMapped) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096 * 2,
    .map_page_count = 1,
    .data_size = 4090,  // Only 6 bytes remaining in page.
    .batch_offset = 0,
    .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, false);
}

TEST_F(TestIsValidBatchBuffer, MultiplePages) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096 * 2,
    .map_page_count = 2,
    .data_size = 4096,  // Data fills the page but there is an additional mapped page.
    .batch_offset = 0,
    .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, true);
}

TEST_F(TestIsValidBatchBuffer, ValidBatchWithOffset) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096,
    .map_page_count = 1,
    .data_size = 4000,  // With the start offset, there are 8 bytes remaining.
    .batch_offset = 88,
    .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, true);
}

TEST_F(TestIsValidBatchBuffer, InvalidBatchWithOffset) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096,
    .map_page_count = 1,
    .data_size = 4008,  // With the start offset, there are no bytes remaining.
    .batch_offset = 88,
    .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, false);
}

TEST_F(TestIsValidBatchBuffer, BatchOffsetNotAligned) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096,
    .map_page_count = 1,
    .data_size = 8,
    .batch_offset = 1,  // Must be 8-byte aligned.
    .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, false);
}
