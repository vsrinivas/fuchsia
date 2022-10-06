// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <shared_mutex>
#include <thread>

#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"

// The test build of the MSD runs a bunch of unit tests automatically when it loads. We need to
// unload the normal MSD to replace it with the test MSD so we can run those tests and query the
// test results.
TEST(UnitTests, UnitTests) {
  auto test_base = std::make_unique<magma::TestDeviceBase>(MAGMA_VENDOR_ID_MALI);
  fidl::ClientEnd parent_device = test_base->GetParentDevice();

  test_base->ShutdownDevice();
  test_base.reset();

  const char* kTestDriverPath = "/system/driver/libmsd_arm_test.so";
  // The test driver will run unit tests on startup.
  magma::TestDeviceBase::BindDriver(parent_device, kTestDriverPath);

  test_base = std::make_unique<magma::TestDeviceBase>(MAGMA_VENDOR_ID_MALI);

  auto result = fidl::WireCall<fuchsia_gpu_magma::TestDevice>(test_base->channel()->borrow())
                    ->GetUnitTestStatus();
  EXPECT_EQ(ZX_OK, result.status()) << "Device connection lost, check syslog for any errors.";
  EXPECT_EQ(ZX_OK, result.value().status) << "Tests reported errors, check syslog.";

  test_base->ShutdownDevice();
  test_base.reset();

  // Reload the production driver so later tests shouldn't be affected.
  magma::TestDeviceBase::AutobindDriver(parent_device);
}
