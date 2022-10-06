// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magma_intel_gen_defs.h>

#include <vector>

#include <gtest/gtest.h>

#include "helper/inflight_list.h"
#include "helper/magma_map_cpu.h"
#include "helper/test_device_helper.h"
#include "magma/magma.h"

namespace {

// Executes multiple simple command buffers over a context/connection.
class TestExecuteWithCount : public testing::TestWithParam<uint32_t> {
 public:
  void SetUp() override {
    base_.InitializeFromVendorId(MAGMA_VENDOR_ID_INTEL);

    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_connection2(base_.device(), &connection_));

    ASSERT_EQ(MAGMA_STATUS_OK, magma_query(base_.device(), kMagmaIntelGenQueryExtraPageCount,
                                           nullptr, &extra_page_count_));

    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_context(connection_, &context_ids_[0]));
    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_context(connection_, &context_ids_[1]));
  }

  void TearDown() override {
    if (context_ids_[0])
      magma_release_context(connection_, context_ids_[0]);
    if (context_ids_[1])
      magma_release_context(connection_, context_ids_[1]);

    if (connection_)
      magma_release_connection(connection_);
  }

  // Validate one command streamer waits for a semaphore, another command streamer signals it.
  void SemaphoreWaitAndSignal(uint32_t context_count) {
    constexpr uint64_t kMapFlags =
        MAGMA_MAP_FLAG_READ | MAGMA_MAP_FLAG_WRITE | MAGMA_MAP_FLAG_EXECUTE;

    constexpr uint32_t kPattern = 0xabcd1234;
    constexpr uint32_t kSize = PAGE_SIZE;

    ASSERT_TRUE(context_count == 1 || context_count == 2);

    uint64_t size;
    magma_buffer_t wait_batch_buffer;
    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_buffer(connection_, kSize, &size, &wait_batch_buffer));

    magma_buffer_t signal_batch_buffer;
    ASSERT_EQ(MAGMA_STATUS_OK,
              magma_create_buffer(connection_, kSize, &size, &signal_batch_buffer));

    magma_buffer_t semaphore_buffer;
    ASSERT_EQ(MAGMA_STATUS_OK, magma_create_buffer(connection_, kSize, &size, &semaphore_buffer));

    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_map_buffer(connection_, gpu_addr_, wait_batch_buffer, 0, size, kMapFlags));

    gpu_addr_ += size + extra_page_count_ * PAGE_SIZE;

    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_map_buffer(connection_, gpu_addr_, signal_batch_buffer, 0, size, kMapFlags));

    gpu_addr_ += size + extra_page_count_ * PAGE_SIZE;

    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_map_buffer(connection_, gpu_addr_, semaphore_buffer, 0, size, kMapFlags));

    // wait for memory location to be > 0
    InitBatchSemaphoreWait(wait_batch_buffer, 0, gpu_addr_);

    // write the memory location
    InitBatchMemoryWrite(signal_batch_buffer, kPattern, gpu_addr_);

    gpu_addr_ += size + extra_page_count_ * PAGE_SIZE;

    // initialize semaphore location to 0
    ClearBuffer(semaphore_buffer, 0);

    magma::InflightList list;

    {
      // Wait for semaphore on render command streamer
      magma_command_descriptor descriptor;
      magma_exec_command_buffer command_buffer;
      std::vector<magma_exec_resource> exec_resources;

      InitCommand(&descriptor, &command_buffer, &exec_resources, wait_batch_buffer,
                  semaphore_buffer);
      descriptor.flags = kMagmaIntelGenCommandBufferForRender;

      EXPECT_EQ(MAGMA_STATUS_OK, magma_execute_command(connection_, context_ids_[0], &descriptor));

      for (auto resource : exec_resources) {
        list.add(resource.buffer_id);
      }
    }

    {
      // Signal semaphore on render command streamer - this command buffer will just be queued on
      // the first context while render CS is blocked on the semaphore.
      magma_command_descriptor descriptor;
      magma_exec_command_buffer command_buffer;
      std::vector<magma_exec_resource> exec_resources;

      InitCommand(&descriptor, &command_buffer, &exec_resources, signal_batch_buffer,
                  semaphore_buffer);
      descriptor.flags = kMagmaIntelGenCommandBufferForRender;

      EXPECT_EQ(MAGMA_STATUS_OK, magma_execute_command(connection_, context_ids_[0], &descriptor));

      for (auto resource : exec_resources) {
        list.add(resource.buffer_id);
      }
    }

    {
      // Signal semaphore on video command streamer - this command buffer executes and unblocks the
      // semaphore.
      magma_command_descriptor descriptor;
      magma_exec_command_buffer command_buffer;
      std::vector<magma_exec_resource> exec_resources;

      InitCommand(&descriptor, &command_buffer, &exec_resources, signal_batch_buffer,
                  semaphore_buffer);
      descriptor.flags = kMagmaIntelGenCommandBufferForVideo;

      uint32_t context = context_count == 2 ? context_ids_[1] : context_ids_[0];

      EXPECT_EQ(MAGMA_STATUS_OK, magma_execute_command(connection_, context, &descriptor));

      for (auto resource : exec_resources) {
        list.add(resource.buffer_id);
      }
    }

    while (list.size()) {
      uint64_t start_size = list.size();

      magma::Status status =
          list.WaitForCompletion(connection_, std::numeric_limits<int64_t>::max());
      ASSERT_EQ(MAGMA_STATUS_OK, status.get());

      list.ServiceCompletions(connection_);

      ASSERT_LT(list.size(), start_size);
    }

    magma_release_buffer(connection_, wait_batch_buffer);
    magma_release_buffer(connection_, signal_batch_buffer);
    magma_release_buffer(connection_, semaphore_buffer);
  }

  enum Mode {
    RENDER_ONLY,
    VIDEO_ONLY,
    RENDER_AND_VIDEO_INTERLEAVED,
  };

  void MemoryWriteAndReadback(Mode mode, uint32_t count, uint32_t context_count) {
    constexpr uint64_t kMapFlags =
        MAGMA_MAP_FLAG_READ | MAGMA_MAP_FLAG_WRITE | MAGMA_MAP_FLAG_EXECUTE;

    constexpr uint32_t kPattern = 0xabcd1234;
    constexpr uint32_t kSize = PAGE_SIZE;

    std::vector<magma_buffer_t> batch_buffers;
    std::vector<magma_buffer_t> result_buffers;

    ASSERT_TRUE(context_count == 1 || context_count == 2);

    for (uint32_t i = 0; i < count; i++) {
      uint64_t size;
      magma_buffer_t batch_buffer;
      ASSERT_EQ(MAGMA_STATUS_OK, magma_create_buffer(connection_, kSize, &size, &batch_buffer));
      batch_buffers.push_back(batch_buffer);

      magma_buffer_t result_buffer;
      ASSERT_EQ(MAGMA_STATUS_OK, magma_create_buffer(connection_, kSize, &size, &result_buffer));
      result_buffers.push_back(result_buffer);

      EXPECT_EQ(MAGMA_STATUS_OK,
                magma_map_buffer(connection_, gpu_addr_, batch_buffer, 0, size, kMapFlags));

      gpu_addr_ += size + extra_page_count_ * PAGE_SIZE;

      EXPECT_EQ(MAGMA_STATUS_OK,
                magma_map_buffer(connection_, gpu_addr_, result_buffer, 0, size, kMapFlags));

      InitBatchMemoryWrite(batch_buffer, kPattern, gpu_addr_);

      gpu_addr_ += size + extra_page_count_ * PAGE_SIZE;

      ClearBuffer(result_buffer, 0xfefefefe);
    }

    magma::InflightList list;

    // Submit everything close together.
    for (uint32_t i = 0; i < count; i++) {
      magma_command_descriptor descriptor;
      magma_exec_command_buffer command_buffer;
      std::vector<magma_exec_resource> exec_resources;

      InitCommand(&descriptor, &command_buffer, &exec_resources, batch_buffers[i],
                  result_buffers[i]);

      switch (mode) {
        case RENDER_ONLY:
          descriptor.flags = kMagmaIntelGenCommandBufferForRender;
          break;
        case VIDEO_ONLY:
          descriptor.flags = kMagmaIntelGenCommandBufferForVideo;
          break;
        case RENDER_AND_VIDEO_INTERLEAVED:
          descriptor.flags = (i % 2 == 0) ? kMagmaIntelGenCommandBufferForRender
                                          : kMagmaIntelGenCommandBufferForVideo;
          break;
      }

      {
        uint32_t context = context_ids_[0];
        if (context_count == 2 && (i % 2)) {
          context = context_ids_[1];
        }
        EXPECT_EQ(MAGMA_STATUS_OK, magma_execute_command(connection_, context, &descriptor));
      }

      for (auto resource : exec_resources) {
        list.add(resource.buffer_id);
      }
    }

    while (list.size()) {
      uint64_t start_size = list.size();

      magma::Status status =
          list.WaitForCompletion(connection_, std::numeric_limits<int64_t>::max());
      ASSERT_EQ(MAGMA_STATUS_OK, status.get());

      list.ServiceCompletions(connection_);

      ASSERT_LT(list.size(), start_size);
    }

    for (uint32_t i = 0; i < count; i++) {
      uint32_t result;
      ReadBufferAt(result_buffers[i], 0, &result);

      EXPECT_EQ(kPattern, result) << " expected: 0x" << std::hex << kPattern << " got: 0x"
                                  << result;

      magma_release_buffer(connection_, batch_buffers[i]);
      magma_release_buffer(connection_, result_buffers[i]);
    }
  }

  // Verifies independent presubmit queueing (pending wait semaphores) for multi engines.
  void MemoryWriteEngineInterleavedPresubmitQueueing(int submit_count, int semaphore_count) {
    ASSERT_EQ(submit_count % 2, 0);

    constexpr uint64_t kMapFlags =
        MAGMA_MAP_FLAG_READ | MAGMA_MAP_FLAG_WRITE | MAGMA_MAP_FLAG_EXECUTE;

    constexpr uint32_t kPattern = 0xabcd1234;
    constexpr uint32_t kSize = PAGE_SIZE;

    constexpr uint64_t kOneSecondInNs = 1000000000ull;

    struct Submit {
      magma_buffer_t batch_buffer;
      magma_buffer_t result_buffer;
      std::vector<magma_semaphore_t> wait_semaphores;
      std::vector<magma_semaphore_t> signal_semaphores;
      uint64_t command_buffer_flags;
    };
    std::vector<Submit> submits;

    for (int i = 0; i < submit_count; i++) {
      Submit submit = {};

      uint64_t size;
      ASSERT_EQ(MAGMA_STATUS_OK,
                magma_create_buffer(connection_, kSize, &size, &submit.batch_buffer));

      ASSERT_EQ(MAGMA_STATUS_OK,
                magma_create_buffer(connection_, kSize, &size, &submit.result_buffer));

      EXPECT_EQ(MAGMA_STATUS_OK,
                magma_map_buffer(connection_, gpu_addr_, submit.batch_buffer, 0, size, kMapFlags));

      gpu_addr_ += size + extra_page_count_ * PAGE_SIZE;

      EXPECT_EQ(MAGMA_STATUS_OK,
                magma_map_buffer(connection_, gpu_addr_, submit.result_buffer, 0, size, kMapFlags));

      InitBatchMemoryWrite(submit.batch_buffer, kPattern, gpu_addr_);

      gpu_addr_ += size + extra_page_count_ * PAGE_SIZE;

      ClearBuffer(submit.result_buffer, 0xfefefefe);

      for (int i = 0; i < semaphore_count; i++) {
        magma_semaphore_t semaphore;

        EXPECT_EQ(MAGMA_STATUS_OK, magma_create_semaphore(connection_, &semaphore));
        submit.wait_semaphores.push_back(semaphore);

        EXPECT_EQ(MAGMA_STATUS_OK, magma_create_semaphore(connection_, &semaphore));
        submit.signal_semaphores.push_back(semaphore);
      }

      // Alternate between engines
      if (i % 2 == 0) {
        submit.command_buffer_flags = kMagmaIntelGenCommandBufferForRender;
      } else {
        submit.command_buffer_flags = kMagmaIntelGenCommandBufferForVideo;
      }

      submits.push_back(std::move(submit));
    }

    magma::InflightList list;

    for (size_t i = 0; i < submits.size(); i++) {
      magma_command_descriptor descriptor;
      magma_exec_command_buffer command_buffer;
      std::vector<magma_exec_resource> exec_resources;

      InitCommand(&descriptor, &command_buffer, &exec_resources, submits[i].batch_buffer,
                  submits[i].result_buffer);

      std::vector<uint64_t> semaphore_ids;
      for (auto& semaphore : submits[i].wait_semaphores) {
        semaphore_ids.push_back(magma_get_semaphore_id(semaphore));
      }
      for (auto& semaphore : submits[i].signal_semaphores) {
        semaphore_ids.push_back(magma_get_semaphore_id(semaphore));
      }
      descriptor.wait_semaphore_count = static_cast<uint32_t>(submits[i].wait_semaphores.size());
      descriptor.signal_semaphore_count =
          static_cast<uint32_t>(submits[i].signal_semaphores.size());
      descriptor.semaphore_ids = semaphore_ids.data();
      descriptor.flags = submits[i].command_buffer_flags;

      {
        uint32_t context = context_ids_[0];
        EXPECT_EQ(MAGMA_STATUS_OK, magma_execute_command(connection_, context, &descriptor));
      }

      for (auto resource : exec_resources) {
        list.add(resource.buffer_id);
      }
    }

    // Ensure signal semaphores not signaled
    for (auto& submit : submits) {
      for (size_t i = 0; i < submit.signal_semaphores.size(); i++) {
        magma_poll_item_t item = {
            .semaphore = submit.signal_semaphores[i],
            .type = MAGMA_POLL_TYPE_SEMAPHORE,
            .condition = MAGMA_POLL_CONDITION_SIGNALED,
        };
        EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, magma_poll(&item, 1, /* timeout_ns= */ 0))
            << "signal semaphore index " << i;
      }
    }

    // Signal wait semaphores for RCS
    for (auto& submit : submits) {
      if (submit.command_buffer_flags == kMagmaIntelGenCommandBufferForRender) {
        for (size_t i = 0; i < submit.wait_semaphores.size(); i++) {
          magma_signal_semaphore(submit.wait_semaphores[i]);
        }
      }
    }

    // Check signal semaphores
    for (auto& submit : submits) {
      for (size_t i = 0; i < submit.signal_semaphores.size(); i++) {
        magma_poll_item_t item = {
            .semaphore = submit.signal_semaphores[i],
            .type = MAGMA_POLL_TYPE_SEMAPHORE,
            .condition = MAGMA_POLL_CONDITION_SIGNALED,
        };
        if (submit.command_buffer_flags == kMagmaIntelGenCommandBufferForRender) {
          EXPECT_EQ(MAGMA_STATUS_OK, magma_poll(&item, 1, kOneSecondInNs))
              << "signal semaphore index " << i;
        } else {
          EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, magma_poll(&item, 1, /* timeout_ns= */ 0))
              << "signal semaphore index " << i;
        }
      }
    }

    // Signal wait semaphores for second engine
    for (auto& submit : submits) {
      if (submit.command_buffer_flags == kMagmaIntelGenCommandBufferForVideo) {
        for (size_t i = 0; i < submit.wait_semaphores.size(); i++) {
          magma_signal_semaphore(submit.wait_semaphores[i]);
        }
      }
    }

    // Check signal semaphores
    for (auto& submit : submits) {
      for (size_t i = 0; i < submit.signal_semaphores.size(); i++) {
        magma_poll_item_t item = {
            .semaphore = submit.signal_semaphores[i],
            .type = MAGMA_POLL_TYPE_SEMAPHORE,
            .condition = MAGMA_POLL_CONDITION_SIGNALED,
        };
        EXPECT_EQ(MAGMA_STATUS_OK, magma_poll(&item, 1, kOneSecondInNs))
            << "signal semaphore index " << i;
      }
    }

    // Check completion notifications
    while (list.size()) {
      uint64_t start_size = list.size();

      magma::Status status =
          list.WaitForCompletion(connection_, std::numeric_limits<int64_t>::max());
      ASSERT_EQ(MAGMA_STATUS_OK, status.get());

      list.ServiceCompletions(connection_);

      ASSERT_LT(list.size(), start_size);
    }

    // Check results and cleanup
    for (size_t i = 0; i < submits.size(); i++) {
      uint32_t result;
      ReadBufferAt(submits[i].result_buffer, 0, &result);

      EXPECT_EQ(kPattern, result) << "submit " << i << " expected: 0x" << std::hex << kPattern
                                  << " got: 0x" << result;

      magma_release_buffer(connection_, submits[i].batch_buffer);
      magma_release_buffer(connection_, submits[i].result_buffer);

      for (auto& semaphore : submits[i].wait_semaphores) {
        magma_release_semaphore(connection_, semaphore);
      }
      for (auto& semaphore : submits[i].signal_semaphores) {
        magma_release_semaphore(connection_, semaphore);
      }
    }
  }

  void ReadBufferAt(magma_buffer_t buffer, uint32_t dword_offset, uint32_t* result_out) {
    uint64_t size = magma_get_buffer_size(buffer);

    void* vaddr;
    ASSERT_TRUE(magma::MapCpuHelper(buffer, 0 /*offset*/, size, &vaddr));

    *result_out = reinterpret_cast<uint32_t*>(vaddr)[dword_offset];

    ASSERT_TRUE(magma::UnmapCpuHelper(vaddr, size));
  }

  void ClearBuffer(magma_buffer_t buffer, uint32_t value) {
    uint64_t size = magma_get_buffer_size(buffer);

    void* vaddr;
    ASSERT_TRUE(magma::MapCpuHelper(buffer, 0 /*offset*/, size, &vaddr));

    for (uint32_t i = 0; i < magma_get_buffer_size(buffer) / sizeof(uint32_t); i++) {
      reinterpret_cast<uint32_t*>(vaddr)[i] = value;
    }

    ASSERT_TRUE(magma::UnmapCpuHelper(vaddr, size));
  }

  void InitBatchMemoryWrite(magma_buffer_t buffer, uint32_t pattern, uint64_t target_gpu_addr) {
    uint64_t size = magma_get_buffer_size(buffer);

    void* vaddr;
    ASSERT_TRUE(magma::MapCpuHelper(buffer, 0 /*offset*/, size, &vaddr));

    memset(vaddr, 0, size);

    {
      auto batch_ptr = reinterpret_cast<uint32_t*>(vaddr);
      *batch_ptr++ = (0x20 << 23)  // command opcode: store dword
                     | 4 - 2;      // number of dwords - 2
      *batch_ptr++ = magma::lower_32_bits(target_gpu_addr);
      *batch_ptr++ = magma::upper_32_bits(target_gpu_addr);
      *batch_ptr++ = pattern;

      *batch_ptr++ = 0xA << 23;  // command opcode: batch end
    }

    ASSERT_TRUE(magma::UnmapCpuHelper(vaddr, size));
  }

  void InitBatchSemaphoreWait(magma_buffer_t buffer, uint32_t pattern, uint64_t target_gpu_addr) {
    uint64_t size = magma_get_buffer_size(buffer);

    void* vaddr;
    ASSERT_TRUE(magma::MapCpuHelper(buffer, 0 /*offset*/, size, &vaddr));

    memset(vaddr, 0, size);

    {
      auto batch_ptr = reinterpret_cast<uint32_t*>(vaddr);

      // wait for value at memory location to be > pattern
      *batch_ptr++ = (0x1C << 23)  // command opcode: wait for semaphore
                     | (1 << 15)   // polling mode
                     | 4 - 2;      // number of dwords - 2
      *batch_ptr++ = pattern;
      *batch_ptr++ = magma::lower_32_bits(target_gpu_addr);
      *batch_ptr++ = magma::upper_32_bits(target_gpu_addr);

      *batch_ptr++ = 0xA << 23;  // command opcode: batch end
    }

    ASSERT_TRUE(magma::UnmapCpuHelper(vaddr, size));
  }

  void InitCommand(magma_command_descriptor* descriptor, magma_exec_command_buffer* command_buffer,
                   std::vector<magma_exec_resource>* exec_resources, magma_buffer_t batch_buffer,
                   magma_buffer_t result_buffer) {
    exec_resources->clear();

    exec_resources->push_back({.buffer_id = magma_get_buffer_id(batch_buffer),
                               .offset = 0,
                               .length = magma_get_buffer_size(batch_buffer)});

    exec_resources->push_back({.buffer_id = magma_get_buffer_id(result_buffer),
                               .offset = 0,
                               .length = magma_get_buffer_size(result_buffer)});

    command_buffer->resource_index = 0;
    command_buffer->start_offset = 0;

    descriptor->resource_count = static_cast<uint32_t>(exec_resources->size());
    descriptor->command_buffer_count = 1;
    descriptor->wait_semaphore_count = 0;
    descriptor->signal_semaphore_count = 0;
    descriptor->resources = exec_resources->data();
    descriptor->command_buffers = command_buffer;
    descriptor->semaphore_ids = nullptr;
    descriptor->flags = 0;
  }

 private:
  magma::TestDeviceBase base_;
  magma_connection_t connection_;
  uint32_t context_ids_[2] = {0};
  uint64_t extra_page_count_ = 0;
  uint64_t gpu_addr_ = 0x10000;
};

class TestExecuteCommandBufferCount : public TestExecuteWithCount {};

TEST_P(TestExecuteCommandBufferCount, RenderOneContext) {
  MemoryWriteAndReadback(RENDER_ONLY, GetParam(), 1);
}

TEST_P(TestExecuteCommandBufferCount, VideoOneContext) {
  MemoryWriteAndReadback(VIDEO_ONLY, GetParam(), 1);
}

TEST_P(TestExecuteCommandBufferCount, RenderAndVideoOneContext) {
  MemoryWriteAndReadback(RENDER_AND_VIDEO_INTERLEAVED, GetParam(), 1);
}

TEST_P(TestExecuteCommandBufferCount, RenderTwoContext) {
  MemoryWriteAndReadback(RENDER_ONLY, GetParam(), 2);
}

TEST_P(TestExecuteCommandBufferCount, VideoTwoContext) {
  MemoryWriteAndReadback(VIDEO_ONLY, GetParam(), 2);
}

TEST_P(TestExecuteCommandBufferCount, RenderAndVideoTwoContext) {
  MemoryWriteAndReadback(RENDER_AND_VIDEO_INTERLEAVED, GetParam(), 2);
}

INSTANTIATE_TEST_SUITE_P(ExecuteMemoryWriteAndReadback, TestExecuteCommandBufferCount,
                         ::testing::Values(1000), [](testing::TestParamInfo<uint32_t> info) {
                           return std::to_string(info.param);
                         });

class TestExecuteContextCount : public TestExecuteWithCount {};

TEST_P(TestExecuteContextCount, SemaphoreWaitAndSignal) { SemaphoreWaitAndSignal(GetParam()); }

INSTANTIATE_TEST_SUITE_P(ExecuteSemaphore, TestExecuteContextCount, ::testing::Values(1, 2),
                         [](testing::TestParamInfo<uint32_t> info) {
                           return std::to_string(info.param);
                         });

class TestMemoryWriteEngineInterleavedPresubmitQueueing : public TestExecuteWithCount {};

TEST_P(TestMemoryWriteEngineInterleavedPresubmitQueueing, OneSemaphore) {
  MemoryWriteEngineInterleavedPresubmitQueueing(GetParam(), /* semaphore_count= */ 1);
}

TEST_P(TestMemoryWriteEngineInterleavedPresubmitQueueing, ManySemaphore) {
  MemoryWriteEngineInterleavedPresubmitQueueing(GetParam(), /* semaphore_count= */ 3);
}

INSTANTIATE_TEST_SUITE_P(MemoryWriteEngineInterleavedPresubmitQueueing,
                         TestMemoryWriteEngineInterleavedPresubmitQueueing, ::testing::Values(2, 4),
                         [](testing::TestParamInfo<uint32_t> info) {
                           return std::to_string(info.param);
                         });

}  // namespace
