// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <thread>

#include <gtest/gtest.h>

#include "helper/inflight_list.h"
#include "helper/magma_map_cpu.h"
#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_intel_gen_defs.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace {

constexpr uint64_t kMapFlags = MAGMA_MAP_FLAG_READ | MAGMA_MAP_FLAG_WRITE | MAGMA_MAP_FLAG_EXECUTE;

constexpr uint32_t kValue = 0xabcddcba;

class TestConnection : public magma::TestDeviceBase {
 public:
  TestConnection() : magma::TestDeviceBase(MAGMA_VENDOR_ID_INTEL) {
    magma_create_connection2(device(), &connection_);
    DASSERT(connection_);

    magma_status_t status =
        magma_query(device(), kMagmaIntelGenQueryExtraPageCount, nullptr, &extra_page_count_);
    if (status != MAGMA_STATUS_OK) {
      DLOG("Failed to query kMagmaIntelGenQueryExtraPageCount: %d", status);
      extra_page_count_ = 0;
    }

    magma_create_context(connection_, &context_id_);
  }

  ~TestConnection() {
    if (connection_) {
      magma_release_context(connection_, context_id_);
      magma_release_connection(connection_);
    }
  }

  enum How { NORMAL, FAULT, HANG };

  static constexpr bool kUseGlobalGtt = false;
  static constexpr int64_t kOneSecondInNs = 1000000000;
  static constexpr uint64_t kUnmappedBufferGpuAddress = 0x1000000;  // arbitrary

  void SubmitCommandBuffer(How how, uint64_t flags) {
    ASSERT_NE(connection_, 0u);

    uint64_t buffer_size;
    magma_buffer_t batch_buffer;

    ASSERT_EQ(MAGMA_STATUS_OK,
              magma_create_buffer(connection_, PAGE_SIZE, &buffer_size, &batch_buffer));
    void* vaddr;
    ASSERT_TRUE(magma::MapCpuHelper(batch_buffer, 0 /*offset*/, buffer_size, &vaddr));

    ASSERT_EQ(MAGMA_STATUS_OK, magma_map_buffer(connection_, gpu_addr_, batch_buffer, 0,
                                                magma::page_size(), kMapFlags));

    // Write to the last dword
    InitBatchBuffer(
        vaddr, buffer_size, how == HANG,
        how == FAULT ? kUnmappedBufferGpuAddress : gpu_addr_ + buffer_size - sizeof(uint32_t));

    // Increment gpu address for next iteration
    gpu_addr_ += (1 + extra_page_count_) * PAGE_SIZE;

    magma_command_descriptor descriptor;
    magma_exec_command_buffer command_buffer;
    magma_exec_resource exec_resource;
    EXPECT_TRUE(InitCommand(&descriptor, &command_buffer, &exec_resource, batch_buffer, buffer_size,
                            flags));
    EXPECT_EQ(MAGMA_STATUS_OK, magma_execute_command(connection_, context_id_, &descriptor));

    magma::InflightList list;

    switch (how) {
      case NORMAL:
        EXPECT_TRUE(list.WaitForCompletion(connection_, kOneSecondInNs));
        EXPECT_EQ(MAGMA_STATUS_OK, magma_get_error(connection_));
        EXPECT_EQ(kValue, reinterpret_cast<uint32_t*>(vaddr)[buffer_size / 4 - 1]);
        break;
      case FAULT: {
        // Intel won't actually fault because bad gpu addresses are valid
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() -
                                                         start)
                   .count() < 2000) {
          if (magma_get_error(connection_) == MAGMA_STATUS_CONNECTION_LOST) {
            break;
          }
        }
        EXPECT_EQ(MAGMA_STATUS_CONTEXT_KILLED, magma_get_error(connection_));
        EXPECT_TRUE(list.WaitForCompletion(connection_, kOneSecondInNs));
        EXPECT_EQ(0xdeadbeef, reinterpret_cast<uint32_t*>(vaddr)[buffer_size / 4 - 1]);
        break;
      }
      case HANG: {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() -
                                                         start)
                   .count() < 2000) {
          if (magma_get_error(connection_) == MAGMA_STATUS_CONNECTION_LOST) {
            break;
          }
        }
        EXPECT_EQ(MAGMA_STATUS_CONTEXT_KILLED, magma_get_error(connection_));
        EXPECT_TRUE(list.WaitForCompletion(connection_, kOneSecondInNs));
        EXPECT_EQ(kValue, reinterpret_cast<uint32_t*>(vaddr)[buffer_size / 4 - 1]);
        break;
      }
    }

    EXPECT_TRUE(magma::UnmapCpuHelper(vaddr, buffer_size));

    magma_release_buffer(connection_, batch_buffer);
  }

  void InitBatchBuffer(void* vaddr, uint64_t size, bool hang, uint64_t gpu_addr) {
    memset(vaddr, 0, size);

    constexpr uint32_t kStoreDwordOp = 0x20 << 23;
    constexpr uint32_t kStoreDwordCount = 4 - 2;  // always -2
    reinterpret_cast<uint32_t*>(vaddr)[0] =
        kStoreDwordOp | kStoreDwordCount | (kUseGlobalGtt ? 1 << 22 : 0);
    reinterpret_cast<uint32_t*>(vaddr)[1] = gpu_addr & 0xffffffff;
    reinterpret_cast<uint32_t*>(vaddr)[2] = gpu_addr >> 32;
    reinterpret_cast<uint32_t*>(vaddr)[3] = kValue;

    constexpr uint32_t kWaitForSemaphoreOp = 0x1C << 23;
    constexpr uint32_t kWaitForSemaphoreCount = 4 - 2;  // always -2
    // wait for semaphore - proceed if dword at given address > dword given
    reinterpret_cast<uint32_t*>(vaddr)[4] =
        kWaitForSemaphoreOp | kWaitForSemaphoreCount | (kUseGlobalGtt ? 1 << 22 : 0);
    reinterpret_cast<uint32_t*>(vaddr)[5] = hang ? ~0 : 0;
    reinterpret_cast<uint32_t*>(vaddr)[6] = gpu_addr & 0xffffffff;
    reinterpret_cast<uint32_t*>(vaddr)[7] = gpu_addr >> 32;

    constexpr uint32_t kEndBatchOp = 0xA << 23;
    reinterpret_cast<uint32_t*>(vaddr)[8] = kEndBatchOp;

    // initialize scratch memory location
    reinterpret_cast<uint32_t*>(vaddr)[size / 4 - 1] = 0xdeadbeef;
  }

  bool InitCommand(magma_command_descriptor* descriptor, magma_exec_command_buffer* command_buffer,
                   magma_exec_resource* exec_resource, magma_buffer_t batch_buffer,
                   uint64_t batch_buffer_length, uint64_t flags) {
    exec_resource->buffer_id = magma_get_buffer_id(batch_buffer);
    exec_resource->offset = 0;
    exec_resource->length = batch_buffer_length;

    command_buffer->resource_index = 0;
    command_buffer->start_offset = 0;

    descriptor->resource_count = 1;
    descriptor->command_buffer_count = 1;
    descriptor->wait_semaphore_count = 0;
    descriptor->signal_semaphore_count = 0;
    descriptor->resources = exec_resource;
    descriptor->command_buffers = command_buffer;
    descriptor->semaphore_ids = nullptr;
    descriptor->flags = flags;

    return true;
  }

  static void Stress(uint32_t iterations, uint64_t flags) {
    for (uint32_t i = 0; i < iterations; i++) {
      DLOG("iteration %d/%d", i, iterations);
      std::thread happy([flags] {
        std::unique_ptr<TestConnection> test(new TestConnection());
        for (uint32_t count = 0; count < 100; count++) {
          test->SubmitCommandBuffer(TestConnection::NORMAL, flags);
        }
      });

      std::thread sad([flags] {
        std::unique_ptr<TestConnection> test(new TestConnection());
        for (uint32_t count = 0; count < 100; count++) {
          if (count % 2 == 0) {
            test->SubmitCommandBuffer(TestConnection::NORMAL, flags);
          } else if (count % 3 == 0) {
            test->SubmitCommandBuffer(TestConnection::FAULT, flags);
            test.reset(new TestConnection());
          } else {
            test->SubmitCommandBuffer(TestConnection::HANG, flags);
            test.reset(new TestConnection());
          }
        }
      });

      happy.join();
      sad.join();
    }
  }

  void SubmitAndDisconnect(uint64_t flags) {
    uint64_t size;
    magma_buffer_t batch_buffer;

    ASSERT_EQ(magma_create_buffer(connection_, PAGE_SIZE, &size, &batch_buffer), 0);
    void* vaddr;
    ASSERT_TRUE(magma::MapCpuHelper(batch_buffer, 0 /*offset*/, size, &vaddr));

    InitBatchBuffer(vaddr, size, true, kUnmappedBufferGpuAddress);

    magma_command_descriptor descriptor;
    magma_exec_command_buffer command_buffer;
    magma_exec_resource exec_resource;
    EXPECT_TRUE(
        InitCommand(&descriptor, &command_buffer, &exec_resource, batch_buffer, size, flags));
    EXPECT_EQ(MAGMA_STATUS_OK, magma_execute_command(connection_, context_id_, &descriptor));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(magma::UnmapCpuHelper(vaddr, size));
    magma_release_buffer(connection_, batch_buffer);

    magma_release_connection(connection_);
    connection_ = 0u;
  }

 private:
  magma_connection_t connection_;
  uint32_t context_id_;
  uint64_t extra_page_count_ = 0;
  uint64_t gpu_addr_ = 0;
};

}  // namespace

class TestHangRecovery : public testing::TestWithParam<uint64_t> {};

TEST_P(TestHangRecovery, Hang) {
  uint64_t flags = GetParam();
  { TestConnection().SubmitCommandBuffer(TestConnection::HANG, flags); }
}

TEST_P(TestHangRecovery, Fault) {
  uint64_t flags = GetParam();
  { TestConnection().SubmitCommandBuffer(TestConnection::FAULT, flags); }
}

TEST_P(TestHangRecovery, Sequence) {
  uint64_t flags = GetParam();
  { TestConnection().SubmitCommandBuffer(TestConnection::NORMAL, flags); }
  { TestConnection().SubmitCommandBuffer(TestConnection::FAULT, flags); }
  { TestConnection().SubmitCommandBuffer(TestConnection::NORMAL, flags); }
  { TestConnection().SubmitCommandBuffer(TestConnection::HANG, flags); }
  { TestConnection().SubmitCommandBuffer(TestConnection::NORMAL, flags); }
}

TEST_P(TestHangRecovery, SubmitAndDisconnect) {
  uint64_t flags = GetParam();
  TestConnection().SubmitAndDisconnect(flags);
}

INSTANTIATE_TEST_SUITE_P(TestHangRecovery, TestHangRecovery,
                         testing::Values(kMagmaIntelGenCommandBufferForRender,
                                         kMagmaIntelGenCommandBufferForVideo),
                         [](testing::TestParamInfo<uint64_t> info) {
                           switch (info.param) {
                             case kMagmaIntelGenCommandBufferForRender:
                               return "Render";
                             case kMagmaIntelGenCommandBufferForVideo:
                               return "Video";
                           }
                           return "Unknown";
                         });

TEST(HangRecovery, DISABLED_Stress) {
  TestConnection::Stress(1000, kMagmaIntelGenCommandBufferForRender);
}
