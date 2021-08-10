// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/service/llcpp/service.h>

#include <gtest/gtest.h>

TEST(DdkFirmwaretest, DriverWasLoaded) {
  fbl::unique_fd dev(open("/dev", O_RDONLY));
  ASSERT_TRUE(dev);

  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK,
            devmgr_integration_test::RecursiveWaitForFile(dev, "sys/test/ddk-fallback-test", &out));
}
