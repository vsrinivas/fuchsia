// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/vmo.h>

#include <thread>

#include <gtest/gtest.h>

#include "../vkreadback/vkreadback.h"
#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_vendor_queries.h"
#include "src/lib/fxl/test/test_settings.h"

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(VulkanTimerQuery, TimerQuery) {
  magma::TestDeviceBase test_device(MAGMA_VENDOR_ID_MALI);
  ;

  uint64_t is_supported = 0;
  EXPECT_EQ(MAGMA_STATUS_OK, magma_query(test_device.device(), MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED,
                                         nullptr, &is_supported));

  // Every Mali driver should support querying GPU time.
  EXPECT_TRUE(is_supported);

  zx::vmo result_vmo;
  EXPECT_EQ(MAGMA_STATUS_OK, magma_query(test_device.device(), MAGMA_QUERY_TOTAL_TIME,
                                         result_vmo.reset_and_get_address(), nullptr));
  magma_total_time_query_result result;
  EXPECT_EQ(ZX_OK, result_vmo.read(&result, 0u, sizeof(result)));

  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize(VK_API_VERSION_1_1));
  ASSERT_TRUE(test.Exec());
  ASSERT_TRUE(test.Readback());

  EXPECT_EQ(MAGMA_STATUS_OK, magma_query(test_device.device(), MAGMA_QUERY_TOTAL_TIME,
                                         result_vmo.reset_and_get_address(), nullptr));
  magma_total_time_query_result result2;
  EXPECT_EQ(ZX_OK, result_vmo.read(&result2, 0u, sizeof(result2)));

  // Both GPU and CPU time should have passed.
  EXPECT_LT(result.gpu_time_ns, result2.gpu_time_ns);
  EXPECT_LT(result.monotonic_time_ns, result2.monotonic_time_ns);
}
