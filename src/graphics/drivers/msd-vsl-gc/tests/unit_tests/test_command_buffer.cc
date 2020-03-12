// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_command_buffer.h"

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "src/graphics/drivers/msd-vsl-gc/src/command_buffer.h"
#include "src/graphics/drivers/msd-vsl-gc/src/instructions.h"
#include "src/graphics/drivers/msd-vsl-gc/src/msd_vsl_device.h"

void TestCommandBuffer::CreateMsdBuffer(uint32_t buffer_size,
                                        std::shared_ptr<MsdVslBuffer>* out_buffer) {
  std::unique_ptr<magma::PlatformBuffer> buffer =
      magma::PlatformBuffer::Create(buffer_size, "test buffer");
  ASSERT_NE(buffer, nullptr);

  ASSERT_TRUE(buffer->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED));

  auto msd_buffer = std::make_shared<MsdVslBuffer>(std::move(buffer));
  ASSERT_NE(msd_buffer, nullptr);

  *out_buffer = msd_buffer;
}

void TestCommandBuffer::CreateAndMapBuffer(std::shared_ptr<MsdVslContext> context,
                                           uint32_t buffer_size, uint32_t map_page_count,
                                           uint32_t gpu_addr,
                                           std::shared_ptr<MsdVslBuffer>* out_buffer) {
  std::shared_ptr<MsdVslBuffer> msd_buffer;
  ASSERT_NO_FATAL_FAILURE(CreateMsdBuffer(buffer_size, &msd_buffer));

  std::shared_ptr<GpuMapping> gpu_mapping;
  magma::Status status =
      AddressSpace::MapBufferGpu(context->exec_address_space(), msd_buffer, gpu_addr,
                                 0 /* page_offset */, map_page_count, &gpu_mapping);
  ASSERT_TRUE(status.ok());
  ASSERT_NE(gpu_mapping, nullptr);

  ASSERT_TRUE(context->exec_address_space()->AddMapping(std::move(gpu_mapping)));

  *out_buffer = msd_buffer;
}

void TestCommandBuffer::CreateAndPrepareBatch(std::shared_ptr<MsdVslContext> context,
                                              std::shared_ptr<MsdVslBuffer> buffer,
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
  auto batch = std::make_unique<CommandBuffer>(context, 0, std::move(command_buffer));
  ASSERT_NE(batch, nullptr);

  std::vector<CommandBuffer::ExecResource> resources;
  resources.emplace_back(
      CommandBuffer::ExecResource{.buffer = buffer, .offset = 0, .length = data_size});

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

void TestCommandBuffer::CreateAndSubmitBuffer(std::shared_ptr<MsdVslContext> context,
                                              const BufferDesc& buffer_desc,
                                              std::shared_ptr<MsdVslBuffer>* out_buffer) {
  std::shared_ptr<MsdVslBuffer> buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(
      context, buffer_desc.buffer_size, buffer_desc.map_page_count, buffer_desc.gpu_addr, &buffer));

  // Write a WAIT command at offset |kBatchOffset|.
  WriteWaitCommand(buffer, buffer_desc.batch_offset);

  // Submit the batch and verify we get a completion event.
  auto semaphore = magma::PlatformSemaphore::Create();
  ASSERT_NE(semaphore, nullptr);

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(context, buffer, buffer_desc.data_size,
                                                buffer_desc.batch_offset, semaphore->Clone(),
                                                &batch));
  ASSERT_TRUE(batch->IsValidBatchBuffer());

  ASSERT_TRUE(context->SubmitBatch(std::move(batch)).ok());

  constexpr uint64_t kTimeoutMs = 1000;
  ASSERT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());

  if (out_buffer) {
    *out_buffer = buffer;
  }
}

void TestCommandBuffer::WriteWaitCommand(std::shared_ptr<MsdVslBuffer> buffer, uint32_t offset) {
  uint32_t* cmd_ptr;
  ASSERT_TRUE(buffer->platform_buffer()->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));
  BufferWriter buf_writer(cmd_ptr, buffer->platform_buffer()->size(), offset);
  MiWait::write(&buf_writer);
  ASSERT_TRUE(buffer->platform_buffer()->UnmapCpu());
}

// Unit tests for |IsValidBatchBatch|.
class TestIsValidBatchBuffer : public TestCommandBuffer {
 public:
  void DoTest(const BufferDesc& buffer_desc, bool want_is_valid) {
    std::shared_ptr<MsdVslBuffer> buffer;
    ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(default_context(), buffer_desc.buffer_size,
                                               buffer_desc.map_page_count, buffer_desc.gpu_addr,
                                               &buffer));

    std::unique_ptr<CommandBuffer> batch;
    ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(default_context(), buffer, buffer_desc.data_size,
                                                  buffer_desc.batch_offset, nullptr /* signal */,
                                                  &batch));
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
