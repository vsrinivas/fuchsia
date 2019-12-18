// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "magma_util/dlog.h"

void magma_indriver_test(magma::PlatformPciDevice* platform_device) {
  DLOG("running magma unit tests");
  TestPlatformPciDevice::SetInstance(platform_device);
  SetTestDeviceHandle(platform_device->GetDeviceHandle());
  const int kArgc = 3;
  const char* argv[kArgc] = {"magma_indriver_test", "--gtest_filter=-PlatformDevice*.*"};
  testing::InitGoogleTest(const_cast<int*>(&kArgc), const_cast<char**>(argv));

  printf("[DRV START=]\n");
  (void)RUN_ALL_TESTS();
  printf("[DRV END===]\n[==========]\n");
}
