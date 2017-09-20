// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include "helper/platform_device_helper.h"
#include "magma_util/dlog.h"
#include "gtest/gtest.h"
#include <ddk/device.h>

void magma_indriver_test(zx_device_t* device)
{
    DLOG("running magma unit tests");
    TestPlatformDevice::SetInstance(magma::PlatformDevice::Create(device));
    const int kArgc = 2;
    const char* argv[kArgc] = {"magma_indriver_test", "--gtest_output=xml:/data/test_out/"};
    testing::InitGoogleTest(const_cast<int*>(&kArgc), const_cast<char**>(argv));

    printf("[DRV START=]\n");
    (void)RUN_ALL_TESTS();
    printf("[DRV END===]\n[==========]\n");
}
