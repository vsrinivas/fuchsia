// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <lib/ddk/device.h>

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "magma_util/dlog.h"

zx_status_t magma_indriver_test(zx_device_t* device) {
  DLOG("running magma unit tests");
  TestPlatformDevice::SetInstance(magma::PlatformDevice::Create(device));
  SetTestDeviceHandle(device);
  const int kArgc = 3;
  const char* argv[kArgc] = {"magma_indriver_test", "--gtest_filter=-PlatformPci*.*"};
  testing::InitGoogleTest(const_cast<int*>(&kArgc), const_cast<char**>(argv));

  printf("[DRV START=]\n");
  zx_status_t status = RUN_ALL_TESTS() == 0 ? ZX_OK : ZX_ERR_INTERNAL;
  printf("[DRV END===]\n[==========]\n");
  return status;
}
