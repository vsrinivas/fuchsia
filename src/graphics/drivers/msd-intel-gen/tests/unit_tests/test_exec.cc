// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "msd_intel_device.h"
#include "test_command_buffer.h"

class ContextRelease {
 public:
  ContextRelease(std::shared_ptr<ClientContext> context) : context_(context) {}

  ~ContextRelease() { context_->Shutdown(); }

 private:
  std::shared_ptr<ClientContext> context_;
};

class TestExec {
 public:
  void GlobalGttReuseGpuAddress() { ExecReuseGpuAddress(true); }

  void PerProcessGttReuseGpuAddress() { ExecReuseGpuAddress(false); }

  // Submits a few command buffers through the full connection-context flow.
  // Uses per process gtt unless |kUseGlobalGtt| is specified.
  void ExecReuseGpuAddress(const bool kUseGlobalGtt) {
    magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
    ASSERT_NE(platform_device, nullptr);

    std::unique_ptr<MsdIntelDevice> device(
        MsdIntelDevice::Create(platform_device->GetDeviceHandle(), true));
    ASSERT_NE(device, nullptr);

    auto connection =
        std::shared_ptr<MsdIntelConnection>(MsdIntelConnection::Create(device.get(), 1));
    ASSERT_NE(connection, nullptr);

    auto address_space = kUseGlobalGtt ? device->gtt() : connection->per_process_gtt();

    auto context = std::make_shared<ClientContext>(connection, address_space);
    ASSERT_NE(context, nullptr);
    ContextRelease context_release(context);

    // Semaphore for signalling command buffer completion
    auto semaphore = std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());

    // Create batch buffer
    auto batch_buffer = std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "batch"));
    std::shared_ptr<GpuMapping> batch_mapping;
    if (kUseGlobalGtt) {
      batch_mapping = AddressSpace::MapBufferGpu(address_space, batch_buffer);
    } else {
      EXPECT_TRUE(AddressSpace::MapBufferGpu(address_space, batch_buffer, 0x1000, 0,
                                             batch_buffer->platform_buffer()->size() / PAGE_SIZE,
                                             &batch_mapping));
    }
    ASSERT_TRUE(batch_mapping);
    EXPECT_TRUE(address_space->AddMapping(batch_mapping));

    // Send a no-op batch to get the context initialized.
    {
      void* batch_cpu_addr;
      ASSERT_TRUE(batch_mapping->buffer()->platform_buffer()->MapCpu(&batch_cpu_addr));
      auto batch_ptr = reinterpret_cast<uint32_t*>(batch_cpu_addr);
      *batch_ptr++ = 0;
      *batch_ptr++ = 0;
      *batch_ptr++ = 0;
      *batch_ptr++ = 0;
      *batch_ptr++ = (0xA << 23);  // batch end
    }

    std::unique_ptr<CommandBuffer> command_buffer;
    // Create a command buffer
    {
      auto buffer = MsdIntelBuffer::Create(PAGE_SIZE, "cmd buf");
      void* vaddr;
      ASSERT_TRUE(buffer->platform_buffer()->MapCpu(&vaddr));

      auto cmd_buf = static_cast<magma_system_command_buffer*>(vaddr);
      cmd_buf->batch_buffer_resource_index = 0;
      cmd_buf->batch_start_offset = 0;
      cmd_buf->resource_count = 1;
      cmd_buf->wait_semaphore_count = 0;
      cmd_buf->signal_semaphore_count = 1;
      auto semaphores = reinterpret_cast<uint64_t*>(cmd_buf + 1);
      semaphores[0] = semaphore->id();
      // Batch buffer
      auto resources = reinterpret_cast<magma_system_exec_resource*>(semaphores + 1);
      resources[0].buffer_id = batch_buffer->platform_buffer()->id();
      resources[0].offset = 0;
      resources[0].length = batch_buffer->platform_buffer()->size();

      command_buffer =
          TestCommandBuffer::Create(std::move(buffer), context, {batch_buffer}, {}, {semaphore});
      ASSERT_NE(nullptr, command_buffer);
    }

    EXPECT_TRUE(command_buffer->PrepareForExecution());
    EXPECT_TRUE(context->SubmitCommandBuffer(std::move(command_buffer)));
    EXPECT_TRUE(semaphore->Wait(1000));

    // Create two destination buffers, but only one mapping because we want to reuse the same
    // gpu address.
    std::vector<std::shared_ptr<MsdIntelBuffer>> dst_buffer(2);
    dst_buffer[0] = std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "dst0"));
    dst_buffer[1] = std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "dst1"));

    // Initialize the target
    std::vector<void*> dst_cpu_addr(2);
    ASSERT_TRUE(dst_buffer[0]->platform_buffer()->MapCpu(&dst_cpu_addr[0]));
    ASSERT_TRUE(dst_buffer[1]->platform_buffer()->MapCpu(&dst_cpu_addr[1]));

    // Map the first buffer
    std::vector<std::shared_ptr<GpuMapping>> dst_mapping(2);
    if (kUseGlobalGtt) {
      dst_mapping[0] = AddressSpace::MapBufferGpu(address_space, dst_buffer[0]);
    } else {
      EXPECT_TRUE(AddressSpace::MapBufferGpu(address_space, dst_buffer[0], 0x10000, 0,
                                             dst_buffer[0]->platform_buffer()->size() / PAGE_SIZE,
                                             &dst_mapping[0]));
    }
    ASSERT_TRUE(dst_mapping[0]);
    EXPECT_TRUE(address_space->AddMapping(dst_mapping[0]));

    // Initialize the batch buffer
    constexpr uint32_t kExpectedVal = 12345678;
    {
      void* batch_cpu_addr;
      ASSERT_TRUE(batch_mapping->buffer()->platform_buffer()->MapCpu(&batch_cpu_addr));

      static constexpr uint32_t kDwordCount = 4;
      auto batch_ptr = reinterpret_cast<uint32_t*>(batch_cpu_addr);
      *batch_ptr++ =
          (0x20 << 23) | (kDwordCount - 2) | (kUseGlobalGtt ? 1 << 22 : 0);  // store dword
      *batch_ptr++ = magma::lower_32_bits(dst_mapping[0]->gpu_addr());
      *batch_ptr++ = magma::upper_32_bits(dst_mapping[0]->gpu_addr());
      *batch_ptr++ = kExpectedVal;
      *batch_ptr++ = (0xA << 23);  // batch end
    }

    constexpr uint32_t kInitVal = 0xdeadbeef;

    reinterpret_cast<uint32_t*>(dst_cpu_addr[0])[0] = kInitVal;
    reinterpret_cast<uint32_t*>(dst_cpu_addr[1])[0] = kInitVal;

    // Create a command buffer writing to buffer 0
    {
      auto buffer = MsdIntelBuffer::Create(PAGE_SIZE, "cmd buf");
      void* vaddr;
      ASSERT_TRUE(buffer->platform_buffer()->MapCpu(&vaddr));

      auto cmd_buf = static_cast<magma_system_command_buffer*>(vaddr);
      cmd_buf->batch_buffer_resource_index = 0;
      cmd_buf->batch_start_offset = 0;
      cmd_buf->resource_count = 2;
      cmd_buf->wait_semaphore_count = 0;
      cmd_buf->signal_semaphore_count = 1;
      auto semaphores = reinterpret_cast<uint64_t*>(cmd_buf + 1);
      semaphores[0] = semaphore->id();
      // Batch buffer
      auto resources = reinterpret_cast<magma_system_exec_resource*>(semaphores + 1);
      resources[0].buffer_id = batch_buffer->platform_buffer()->id();
      resources[0].offset = 0;
      resources[0].length = batch_buffer->platform_buffer()->size();
      // Destination buffer
      resources[1].buffer_id = dst_buffer[0]->platform_buffer()->id();
      resources[1].offset = 0;
      resources[1].length = dst_buffer[0]->platform_buffer()->size();

      command_buffer = TestCommandBuffer::Create(std::move(buffer), context,
                                                 {batch_buffer, dst_buffer[0]}, {}, {semaphore});
      ASSERT_NE(nullptr, command_buffer);
    }

    EXPECT_EQ(2u, dst_mapping[0].use_count());
    EXPECT_TRUE(command_buffer->PrepareForExecution());
    EXPECT_EQ(3u, dst_mapping[0].use_count());
    EXPECT_TRUE(context->SubmitCommandBuffer(std::move(command_buffer)));
    EXPECT_TRUE(semaphore->Wait(1000));
    EXPECT_EQ(2u, dst_mapping[0].use_count());
    EXPECT_EQ(kExpectedVal, reinterpret_cast<uint32_t*>(dst_cpu_addr[0])[0]);
    EXPECT_EQ(kInitVal, reinterpret_cast<uint32_t*>(dst_cpu_addr[1])[0]);

    // Release the first buffer, map the second
    {
      gpu_addr_t gpu_addr = dst_mapping[0]->gpu_addr();

      dst_mapping[0].reset();

      if (kUseGlobalGtt) {
        std::vector<std::shared_ptr<GpuMapping>> mappings;
        address_space->ReleaseBuffer(dst_buffer[0]->platform_buffer(), &mappings);
        mappings.clear();
      } else {
        // Connection always releases on per_process_gtt
        connection->ReleaseBuffer(dst_buffer[0]->platform_buffer());
      }

      if (kUseGlobalGtt) {
        dst_mapping[1] = AddressSpace::MapBufferGpu(address_space, dst_buffer[1]);
      } else {
        EXPECT_TRUE(AddressSpace::MapBufferGpu(address_space, dst_buffer[1], gpu_addr, 0,
                                               dst_buffer[1]->platform_buffer()->size() / PAGE_SIZE,
                                               &dst_mapping[1]));
      }
      ASSERT_TRUE(dst_mapping[1]);
      EXPECT_TRUE(address_space->AddMapping(dst_mapping[1]));

      ASSERT_EQ(gpu_addr, dst_mapping[1]->gpu_addr());
    }

    reinterpret_cast<uint32_t*>(dst_cpu_addr[0])[0] = kInitVal;
    reinterpret_cast<uint32_t*>(dst_cpu_addr[1])[0] = kInitVal;

    // Create a command buffer writing to buffer 1
    {
      auto buffer = MsdIntelBuffer::Create(PAGE_SIZE, "cmd buf");
      void* vaddr;
      ASSERT_TRUE(buffer->platform_buffer()->MapCpu(&vaddr));

      auto cmd_buf = static_cast<magma_system_command_buffer*>(vaddr);
      cmd_buf->batch_buffer_resource_index = 0;
      cmd_buf->batch_start_offset = 0;
      cmd_buf->resource_count = 2;
      cmd_buf->wait_semaphore_count = 0;
      cmd_buf->signal_semaphore_count = 1;
      auto semaphores = reinterpret_cast<uint64_t*>(cmd_buf + 1);
      semaphores[0] = semaphore->id();
      // Batch buffer
      auto resources = reinterpret_cast<magma_system_exec_resource*>(semaphores + 1);
      resources[0].buffer_id = batch_buffer->platform_buffer()->id();
      resources[0].offset = 0;
      resources[0].length = batch_buffer->platform_buffer()->size();
      // Destination buffer
      resources[1].buffer_id = dst_buffer[1]->platform_buffer()->id();
      resources[1].offset = 0;
      resources[1].length = dst_buffer[1]->platform_buffer()->size();

      command_buffer = TestCommandBuffer::Create(std::move(buffer), context,
                                                 {batch_buffer, dst_buffer[1]}, {}, {semaphore});
      ASSERT_NE(nullptr, command_buffer);
    }

    EXPECT_TRUE(command_buffer->PrepareForExecution());
    EXPECT_TRUE(context->SubmitCommandBuffer(std::move(command_buffer)));
    EXPECT_TRUE(semaphore->Wait(1000));

    EXPECT_EQ(kInitVal, reinterpret_cast<uint32_t*>(dst_cpu_addr[0])[0]);
    EXPECT_EQ(kExpectedVal, reinterpret_cast<uint32_t*>(dst_cpu_addr[1])[0]);
  }
};

TEST(Exec, GlobalGttReuseGpuAddress) { TestExec().GlobalGttReuseGpuAddress(); }

TEST(Exec, PerProcessGttReuseGpuAddress) { TestExec().PerProcessGttReuseGpuAddress(); }
