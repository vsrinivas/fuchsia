// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <thread>

#include "gtest/gtest.h"
#include "helper/test_device_helper.h"
#include "magma.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"

namespace {

class TestConnection : public magma::TestDeviceBase {
 public:
  TestConnection() : magma::TestDeviceBase(MAGMA_VENDOR_ID_MALI) {
    magma_create_connection2(device(), &connection_);
  }

  ~TestConnection() {
    if (connection_)
      magma_release_connection(connection_);
  }

  int32_t Test() {
    DASSERT(connection_);

    uint32_t context_id;
    magma_create_context(connection_, &context_id);

    int32_t result = magma_get_error(connection_);
    if (result != 0)
      return DRET(result);

    magma_execute_immediate_commands2(connection_, context_id, 0, nullptr);

    result = magma_get_error(connection_);
    return DRET(result);
  }

 private:
  magma_connection_t connection_;
};

constexpr uint32_t kMaxCount = 100;
constexpr uint32_t kRestartCount = kMaxCount / 10;

static std::atomic_uint complete_count;

static void looper_thread_entry() {
  std::unique_ptr<TestConnection> test(new TestConnection());
  while (complete_count < kMaxCount) {
    int32_t result = test->Test();
    if (result == 0) {
      complete_count++;
    } else {
      // Wait rendering can't pass back a proper error yet
      EXPECT_TRUE(result == MAGMA_STATUS_CONNECTION_LOST || result == MAGMA_STATUS_INTERNAL_ERROR);
      test.reset(new TestConnection());
    }
  }
}

static void test_shutdown(uint32_t iters) {
  for (uint32_t i = 0; i < iters; i++) {
    complete_count = 0;

    magma::TestDeviceBase test_base(MAGMA_VENDOR_ID_MALI);

    {
      uint64_t is_supported = 0;
      zx_status_t status = fuchsia_gpu_magma_DeviceQuery(
          test_base.channel()->get(), MAGMA_QUERY_IS_TEST_RESTART_SUPPORTED, &is_supported);
      if (status != ZX_OK || !is_supported) {
        printf("Test restart not supported: status %d is_supported %lu\n", status, is_supported);
        return;
      }
    }

    std::thread looper(looper_thread_entry);
    std::thread looper2(looper_thread_entry);
    uint32_t count = kRestartCount;
    while (complete_count < kMaxCount) {
      if (complete_count > count) {
        // Should replace this with a request to devmgr to restart the driver
        EXPECT_EQ(ZX_OK, fuchsia_gpu_magma_DeviceTestRestart(test_base.channel()->get()));

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
