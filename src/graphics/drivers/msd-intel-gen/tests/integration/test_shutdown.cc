// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <magma_intel_gen_defs.h>

#include <shared_mutex>
#include <thread>

#include <gtest/gtest.h>

#include "helper/inflight_list.h"
#include "helper/magma_map_cpu.h"
#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_util/macros.h"

namespace {

class TestConnection : public magma::TestDeviceBase {
 public:
  TestConnection() : magma::TestDeviceBase(MAGMA_VENDOR_ID_INTEL) {
    magma_create_connection2(device(), &connection_);

    magma_status_t status =
        magma_query(device(), kMagmaIntelGenQueryExtraPageCount, nullptr, &extra_page_count_);
    if (status != MAGMA_STATUS_OK) {
      DLOG("Failed to query kMagmaIntelGenQueryExtraPageCount: %d", status);
      extra_page_count_ = 0;
    }
  }

  ~TestConnection() {
    if (connection_)
      magma_release_connection(connection_);
  }

  static constexpr int64_t kOneSecondInNs = 1000000000;

  magma_status_t Test() {
    DASSERT(connection_);

    uint32_t context_id;
    magma::Status status = magma_create_context(connection_, &context_id);
    if (!status.ok())
      return DRET(status.get());

    status = magma_get_error(connection_);
    if (!status.ok())
      return DRET(status.get());

    uint64_t size;
    magma_buffer_t batch_buffer;
    status = magma_create_buffer(connection_, PAGE_SIZE, &size, &batch_buffer);
    if (!status.ok()) {
      magma_release_context(connection_, context_id);
      return DRET(status.get());
    }

    constexpr uint64_t kMapFlags =
        MAGMA_MAP_FLAG_READ | MAGMA_MAP_FLAG_WRITE | MAGMA_MAP_FLAG_EXECUTE;

    status =
        magma_map_buffer(connection_, gpu_addr_, batch_buffer, 0, magma::page_size(), kMapFlags);
    if (!status.ok()) {
      magma_release_context(connection_, context_id);
      magma_release_buffer(connection_, batch_buffer);
      return DRET(status.get());
    }

    gpu_addr_ += (1 + extra_page_count_) * PAGE_SIZE;

    EXPECT_TRUE(InitBatchBuffer(batch_buffer, size));

    magma_command_descriptor descriptor;
    magma_exec_command_buffer command_buffer;
    magma_exec_resource exec_resource;
    EXPECT_TRUE(InitCommand(&descriptor, &command_buffer, &exec_resource, batch_buffer, size));

    status = magma_execute_command(connection_, context_id, &descriptor);
    if (!status.ok()) {
      magma_release_context(connection_, context_id);
      magma_release_buffer(connection_, batch_buffer);
      return DRET(status.get());
    }

    magma::InflightList list;
    status = list.WaitForCompletion(connection_, kOneSecondInNs);
    EXPECT_TRUE(status.get() == MAGMA_STATUS_OK || status.get() == MAGMA_STATUS_CONNECTION_LOST);

    magma_release_context(connection_, context_id);
    magma_release_buffer(connection_, batch_buffer);

    status = magma_get_error(connection_);
    return DRET(status.get());
  }

  bool InitBatchBuffer(magma_buffer_t buffer, uint64_t size) {
    void* vaddr;
    if (!magma::MapCpuHelper(buffer, 0 /*offset*/, size, &vaddr))
      return DRETF(false, "couldn't map batch buffer");

    memset(vaddr, 0, size);

    // Intel end-of-batch
    *reinterpret_cast<uint32_t*>(vaddr) = 0xA << 23;

    EXPECT_TRUE(magma::UnmapCpuHelper(vaddr, size));

    return true;
  }

  bool InitCommand(magma_command_descriptor* descriptor, magma_exec_command_buffer* command_buffer,
                   magma_exec_resource* exec_resource, magma_buffer_t batch_buffer,
                   uint64_t batch_buffer_length) {
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
    descriptor->flags = 0;

    return true;
  }

 private:
  magma_connection_t connection_;
  uint64_t extra_page_count_ = 0;
  uint64_t gpu_addr_ = 0;
};

constexpr uint32_t kMaxCount = 100;
constexpr uint32_t kRestartCount = kMaxCount / 10;

static std::atomic_uint complete_count;
// This lock ensures the looper threads don't continue making new connections while we're attempting
// to unbind, as open connections keep the driver from being released.
static std::shared_mutex connection_create_mutex;

static void looper_thread_entry() {
  std::unique_ptr<TestConnection> test(new TestConnection());
  while (complete_count < kMaxCount) {
    magma_status_t status = test->Test();
    if (status == MAGMA_STATUS_OK) {
      complete_count++;
    } else {
      EXPECT_EQ(status, MAGMA_STATUS_CONNECTION_LOST);
      test.reset();
      std::shared_lock lock(connection_create_mutex);
      test.reset(new TestConnection());
    }
  }
}

static void test_shutdown(uint32_t iters) {
  for (uint32_t i = 0; i < iters; i++) {
    complete_count = 0;

    std::thread looper(looper_thread_entry);
    std::thread looper2(looper_thread_entry);

    uint32_t count = kRestartCount;
    while (complete_count < kMaxCount) {
      if (complete_count > count) {
        // Force looper thread connections to drain. Also prevent loopers from trying to create new
        // connections while the device is torn down, just so it's easier to test that device
        // creation is working.
        std::unique_lock lock(connection_create_mutex);

        auto test_base = std::make_unique<magma::TestDeviceBase>(MAGMA_VENDOR_ID_INTEL);
        fidl::ClientEnd parent_device = test_base->GetParentDevice();

        test_base->ShutdownDevice();
        test_base.reset();

        magma::TestDeviceBase::AutobindDriver(parent_device);

        count += kRestartCount;
      }
      std::this_thread::yield();
    }

    looper.join();
    looper2.join();
  }
}

}  // namespace

TEST(Shutdown, Test) { test_shutdown(1); }

TEST(Shutdown, DISABLED_Stress) { test_shutdown(1000); }
