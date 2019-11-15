// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/gpu/magma/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/vmo.h>

#include <thread>

#include "../vkreadback/vkreadback.h"
#include "gtest/gtest.h"
#include "magma.h"
#include "src/lib/fxl/test/test_settings.h"

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(VulkanTimerQuery, TimerQuery) {
  int fd = open("/dev/class/gpu/000", O_RDONLY);
  EXPECT_LE(0, fd);
  fdio_t* fdio = fdio_unsafe_fd_to_io(fd);

  EXPECT_TRUE(fdio);

  uint64_t is_supported = 0;
  EXPECT_EQ(ZX_OK,
            fuchsia_gpu_magma_DeviceQuery(fdio_unsafe_borrow_channel(fdio),
                                          MAGMA_QUERY_IS_TOTAL_TIME_SUPPORTED, &is_supported));

  // Every Mali driver should support querying GPU time.
  EXPECT_TRUE(is_supported);

  zx::vmo result_vmo;
  EXPECT_EQ(ZX_OK, fuchsia_gpu_magma_DeviceQueryReturnsBuffer(fdio_unsafe_borrow_channel(fdio),
                                                              MAGMA_QUERY_TOTAL_TIME,
                                                              result_vmo.reset_and_get_address()));
  magma_total_time_query_result result;
  EXPECT_EQ(ZX_OK, result_vmo.read(&result, 0u, sizeof(result)));

  VkReadbackTest test;
  ASSERT_TRUE(test.Initialize());
  ASSERT_TRUE(test.Exec());
  ASSERT_TRUE(test.Readback());

  EXPECT_EQ(ZX_OK, fuchsia_gpu_magma_DeviceQueryReturnsBuffer(fdio_unsafe_borrow_channel(fdio),
                                                              MAGMA_QUERY_TOTAL_TIME,
                                                              result_vmo.reset_and_get_address()));
  magma_total_time_query_result result2;
  EXPECT_EQ(ZX_OK, result_vmo.read(&result2, 0u, sizeof(result2)));

  // Both GPU and CPU time should have passed.
  EXPECT_LT(result.gpu_time_ns, result2.gpu_time_ns);
  EXPECT_LT(result.monotonic_time_ns, result2.monotonic_time_ns);
  fdio_unsafe_release(fdio);
  close(fd);
}
