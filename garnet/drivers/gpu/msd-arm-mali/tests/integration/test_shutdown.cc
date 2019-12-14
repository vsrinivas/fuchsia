// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <shared_mutex>
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
// This lock ensures the looper threads don't continue making new connections while we're attempting
// to unbind, as open connections keep the driver from being released.
static std::shared_mutex connection_create_mutex;

static void looper_thread_entry() {
  std::unique_ptr<TestConnection> test;
  {
    std::shared_lock lock(connection_create_mutex);
    test = std::make_unique<TestConnection>();
  }
  while (complete_count < kMaxCount) {
    int32_t result = test->Test();
    if (result == 0) {
      complete_count++;
    } else {
      // Wait rendering can't pass back a proper error yet
      EXPECT_TRUE(result == MAGMA_STATUS_CONNECTION_LOST || result == MAGMA_STATUS_INTERNAL_ERROR);
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

        auto test_base = std::make_unique<magma::TestDeviceBase>(MAGMA_VENDOR_ID_MALI);
        zx::channel parent_device = test_base->GetParentDevice();

        test_base->ShutdownDevice();
        test_base.reset();

        const char* kDriverPath = "/system/driver/" MSD_ARM_NAME;
        magma::TestDeviceBase::BindDriver(parent_device, kDriverPath);
        count += kRestartCount;
      }
      std::this_thread::yield();
    }

    looper.join();
    looper2.join();
  }
}

}  // namespace

// TODO(fxb/42824): Re-enable when unbind issue is fixed.
TEST(Shutdown, DISABLED_Test) { test_shutdown(1); }

TEST(Shutdown, DISABLED_Stress) { test_shutdown(10); }
