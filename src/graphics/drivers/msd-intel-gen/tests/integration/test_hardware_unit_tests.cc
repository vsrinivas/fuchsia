// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <magma_intel_gen_defs.h>

#include <gtest/gtest.h>

#include "helper/test_device_helper.h"

// The test build of the MSD runs a bunch of unit tests automatically when it loads. We need to
// unload the normal MSD to replace it with the test MSD so we can run those tests and query the
// test results.
// TODO(fxbug.dev/13208) - enable
TEST(HardwareUnitTests, All) {
#if ENABLE_HARDWARE_UNIT_TESTS
  auto test_base = std::make_unique<magma::TestDeviceBase>(MAGMA_VENDOR_ID_INTEL);
  fidl::ClientEnd parent_device = test_base->GetParentDevice();

  test_base->ShutdownDevice();
  test_base.reset();

  const char* kTestDriverPath = "libmsd_intel_test.so";
  // The test driver will run unit tests on startup.
  magma::TestDeviceBase::BindDriver(parent_device, kTestDriverPath);

  test_base = std::make_unique<magma::TestDeviceBase>(MAGMA_VENDOR_ID_INTEL);

  auto result = fidl::WireCall<fuchsia_gpu_magma::TestDevice>(test_base->channel()->borrow())
                    ->GetUnitTestStatus();

  EXPECT_EQ(ZX_OK, result.status()) << "Device connection lost, check syslog for any errors.";
  EXPECT_EQ(ZX_OK, result->status) << "Tests reported errors, check syslog.";

  test_base->ShutdownDevice();
  test_base.reset();

  // Reload the production driver so later tests shouldn't be affected.
  const char* kDriverPath = "fuchsia-pkg://fuchsia.com/msd-intel-gen#driver/libmsd_intel.so";
  magma::TestDeviceBase::BindDriver(parent_device, kDriverPath);
#else
  GTEST_SKIP();
#endif
}
