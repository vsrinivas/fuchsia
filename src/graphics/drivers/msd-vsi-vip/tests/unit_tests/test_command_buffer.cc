// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_command_buffer.h"

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "src/graphics/drivers/msd-vsi-vip/src/command_buffer.h"
#include "src/graphics/drivers/msd-vsi-vip/src/instructions.h"
#include "src/graphics/drivers/msd-vsi-vip/src/msd_vsi_device.h"

void TestCommandBuffer::CreateMsdBuffer(uint32_t buffer_size,
                                        std::shared_ptr<MsdVsiBuffer>* out_buffer) {
  std::unique_ptr<magma::PlatformBuffer> buffer =
      magma::PlatformBuffer::Create(buffer_size, "test buffer");
  ASSERT_NE(buffer, nullptr);

  ASSERT_TRUE(buffer->SetCachePolicy(MAGMA_CACHE_POLICY_UNCACHED));

  auto msd_buffer = std::make_shared<MsdVsiBuffer>(std::move(buffer));
  ASSERT_NE(msd_buffer, nullptr);

  *out_buffer = msd_buffer;
}

void TestCommandBuffer::CreateAndMapBuffer(std::shared_ptr<MsdVsiContext> context,
                                           uint32_t buffer_size, uint32_t map_page_count,
                                           uint32_t gpu_addr,
                                           std::shared_ptr<MsdVsiBuffer>* out_buffer) {
  std::shared_ptr<MsdVsiBuffer> msd_buffer;
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

void TestCommandBuffer::CreateAndPrepareBatch(
    std::shared_ptr<MsdVsiContext> context, std::shared_ptr<MsdVsiBuffer> buffer,
    uint32_t data_size, uint32_t batch_offset, std::shared_ptr<magma::PlatformSemaphore> signal,
    std::optional<CommandBuffer::ExecResource> context_state_buffer,
    std::unique_ptr<CommandBuffer>* out_batch) {
  auto command_buffer = std::make_unique<magma_system_command_buffer>(magma_system_command_buffer{
      .resource_count = 1,
      .batch_buffer_resource_index = 0,
      .batch_start_offset = batch_offset,
      .wait_semaphore_count = 0,
      .signal_semaphore_count = signal ? 1 : 0u,
  });
  std::vector<CommandBuffer::ExecResource> resources;
  resources.emplace_back(
      CommandBuffer::ExecResource{.buffer = buffer, .offset = 0, .length = data_size});
  if (context_state_buffer.has_value()) {
    command_buffer->resource_count++;
    resources.emplace_back(context_state_buffer.value());
  }

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  if (signal) {
    signal_semaphores.push_back(signal);
  }
  auto batch = CommandBuffer::Create(context, 0, std::move(command_buffer), std::move(resources),
                                     std::move(signal_semaphores));
  ASSERT_NE(batch, nullptr);

  ASSERT_TRUE(batch->PrepareForExecution());
  *out_batch = std::move(batch);
}

void TestCommandBuffer::CreateAndSubmitBuffer(
    std::shared_ptr<MsdVsiContext> context, const BufferDesc& buffer_desc,
    std::shared_ptr<magma::PlatformSemaphore> signal, std::optional<uint32_t> fault_addr,
    std::optional<CommandBuffer::ExecResource> context_state_buffer,
    std::shared_ptr<MsdVsiBuffer>* out_buffer) {
  std::shared_ptr<MsdVsiBuffer> buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(
      context, buffer_desc.buffer_size, buffer_desc.map_page_count, buffer_desc.gpu_addr, &buffer));

  if (fault_addr.has_value()) {
    constexpr uint32_t prefetch = 16;  // arbitrary
    WriteLinkCommand(buffer, buffer_desc.batch_offset, prefetch, fault_addr.value());
  } else {
    // Write a WAIT command at offset |kBatchOffset|.
    WriteWaitCommand(buffer, buffer_desc.batch_offset);
  }

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(context, buffer, buffer_desc.data_size,
                                                buffer_desc.batch_offset, signal,
                                                std::move(context_state_buffer), &batch));
  ASSERT_TRUE(batch->IsValidBatch());

  ASSERT_TRUE(context->SubmitBatch(std::move(batch)).ok());

  if (out_buffer) {
    *out_buffer = buffer;
  }
}

void TestCommandBuffer::CreateAndSubmitBuffer(
    std::shared_ptr<MsdVsiContext> context, const BufferDesc& buffer_desc,
    std::optional<CommandBuffer::ExecResource> context_state_buffer,
    std::shared_ptr<MsdVsiBuffer>* out_buffer) {
  // Submit the batch and verify we get a completion event.
  auto semaphore = magma::PlatformSemaphore::Create();
  ASSERT_NE(semaphore, nullptr);

  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(context, buffer_desc, semaphore->Clone(),
                                                std::optional<uint32_t>{} /* fault_addr */,
                                                std::move(context_state_buffer), out_buffer));
  constexpr uint64_t kTimeoutMs = 1000;
  ASSERT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());
}

void TestCommandBuffer::WriteWaitCommand(std::shared_ptr<MsdVsiBuffer> buffer, uint32_t offset) {
  uint32_t* cmd_ptr;
  ASSERT_TRUE(buffer->platform_buffer()->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));
  BufferWriter buf_writer(cmd_ptr, buffer->platform_buffer()->size(), offset);
  MiWait::write(&buf_writer);
  ASSERT_TRUE(buffer->platform_buffer()->UnmapCpu());
}

void TestCommandBuffer::WriteLinkCommand(std::shared_ptr<MsdVsiBuffer> buffer, uint32_t offset,
                                         uint32_t prefetch, uint32_t gpu_addr) {
  uint32_t* cmd_ptr;
  ASSERT_TRUE(buffer->platform_buffer()->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));
  BufferWriter buf_writer(cmd_ptr, buffer->platform_buffer()->size(), offset);
  MiLink::write(&buf_writer, prefetch, gpu_addr);
  ASSERT_TRUE(buffer->platform_buffer()->UnmapCpu());
}

void TestCommandBuffer::WriteEventCommand(
    std::unique_ptr<MsdVsiDevice>& device, std::shared_ptr<MsdVsiContext> context,
    std::shared_ptr<MsdVsiBuffer> buf, uint32_t offset,
    std::unique_ptr<magma::PlatformSemaphore>* out_semaphore) {
  uint32_t event_id;
  ASSERT_TRUE(device->AllocInterruptEvent(true /* free_on_complete */, &event_id));

  // Create a semaphore that will be signalled once the interrupt event is received.
  auto semaphore = magma::PlatformSemaphore::Create();
  ASSERT_NE(semaphore, nullptr);
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores{semaphore->Clone()};
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;

  auto fake_batch = std::make_unique<EventBatch>(context, std::move(wait_semaphores),
                                                 std::move(signal_semaphores));
  fake_batch->SetSequenceNumber(0);
  // Don't call |WriteInterruptEvent| directly as that modifies the ringbuffer.
  device->events_[event_id].submitted = true;
  device->events_[event_id].mapped_batch = std::move(fake_batch);

  uint32_t* buf_cpu_addr;
  ASSERT_TRUE(buf->platform_buffer()->MapCpu(reinterpret_cast<void**>(&buf_cpu_addr)));
  BufferWriter buf_writer(buf_cpu_addr, buf->platform_buffer()->size(), offset);
  MiEvent::write(&buf_writer, event_id);
  ASSERT_TRUE(buf->platform_buffer()->UnmapCpu());

  *out_semaphore = std::move(semaphore);
}

// Unit tests for |IsValidBatch|.
class TestIsValidBatch : public TestCommandBuffer {
 public:
  void DoTest(const BufferDesc& buffer_desc, bool want_is_valid,
              std::optional<CommandBuffer::ExecResource> context_state_buffer = std::nullopt) {
    std::shared_ptr<MsdVsiBuffer> buffer;
    ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(default_context(), buffer_desc.buffer_size,
                                               buffer_desc.map_page_count, buffer_desc.gpu_addr,
                                               &buffer));

    std::unique_ptr<CommandBuffer> batch;
    ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(default_context(), buffer, buffer_desc.data_size,
                                                  buffer_desc.batch_offset, nullptr /* signal */,
                                                  std::move(context_state_buffer), &batch));
    ASSERT_EQ(want_is_valid, batch->IsValidBatch());
  }
};

TEST_F(TestIsValidBatch, ValidBatch) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4088,  // 8 bytes remaining in buffer.
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, true);
}

TEST_F(TestIsValidBatch, BufferTooSmall) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4090,  // Only 6 bytes remaining in buffer.
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, false);
}

TEST_F(TestIsValidBatch, NotEnoughPagesMapped) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096 * 2,
      .map_page_count = 1,
      .data_size = 4090,  // Only 6 bytes remaining in page.
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, false);
}

TEST_F(TestIsValidBatch, MultiplePages) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096 * 2,
      .map_page_count = 2,
      .data_size = 4096,  // Data fills the page but there is an additional mapped page.
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, true);
}

TEST_F(TestIsValidBatch, ValidBatchWithOffset) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4000,  // With the start offset, there are 8 bytes remaining.
      .batch_offset = 88,
      .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, true);
}

TEST_F(TestIsValidBatch, InvalidBatchWithOffset) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4008,  // With the start offset, there are no bytes remaining.
      .batch_offset = 88,
      .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, false);
}

TEST_F(TestIsValidBatch, BatchOffsetNotAligned) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 1,  // Must be 8-byte aligned.
      .gpu_addr = 0x10000,
  };
  DoTest(buffer_desc, false);
}

TEST_F(TestIsValidBatch, ValidContextStateBufferSize) {
  constexpr uint32_t kCsbBufferSize = 4096;
  constexpr uint32_t kCsbMapPageCount = 1;
  constexpr uint32_t kCsbDataSize = 4088;  // 8 bytes remaining in buffer

  std::shared_ptr<MsdVsiBuffer> buf;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndMapBuffer(default_context(), kCsbBufferSize, kCsbMapPageCount, 0x10000, &buf));
  auto csb = std::make_unique<FakeContextStateBuffer>(std::move(buf), kCsbDataSize, nullptr);

  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4088,  // 8 bytes remaining in buffer.
      .batch_offset = 0,
      .gpu_addr = 0x20000,
  };
  DoTest(buffer_desc, true, csb->ExecResource());
}

TEST_F(TestIsValidBatch, InvalidContextStateBufferSize) {
  constexpr uint32_t kCsbBufferSize = 4096;
  constexpr uint32_t kCsbMapPageCount = 1;
  constexpr uint32_t kCsbDataSize = 4092;  // Only 6 bytes remaining in buffer.

  std::shared_ptr<MsdVsiBuffer> buf;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndMapBuffer(default_context(), kCsbBufferSize, kCsbMapPageCount, 0x10000, &buf));
  auto csb = std::make_unique<FakeContextStateBuffer>(std::move(buf), kCsbDataSize, nullptr);

  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4088,  // 8 bytes remaining in buffer.
      .batch_offset = 0,
      .gpu_addr = 0x20000,
  };
  DoTest(buffer_desc, false, csb->ExecResource());
}
