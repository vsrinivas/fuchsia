// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <ddk/device.h>

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "magma_util/dlog.h"

void magma_indriver_test(zx_device_t* device, void* driver_device_handle) {
  DLOG("running magma indriver unit tests");
  SetTestDeviceHandle(driver_device_handle);
  TestPlatformDevice::SetInstance(magma::PlatformDevice::Create(device));
  const int kArgc = 3;
  const char* argv[kArgc] = {"magma_indriver_test",
                             "--gtest_filter=-PlatformPci*.*:MagmaSystem.Multithread:MsdBuffer.*"};
  testing::InitGoogleTest(const_cast<int*>(&kArgc), const_cast<char**>(argv));

  printf("[DRV START=]\n");
  (void)RUN_ALL_TESTS();
  printf("[DRV END===]\n[==========]\n");
}
