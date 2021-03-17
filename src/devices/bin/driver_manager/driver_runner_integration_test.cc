// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>

#include <zxtest/zxtest.h>

TEST(DriverRunnerIntegrationTest, UseDriverRunner) {
  devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
  args.driver_search_paths.push_back("/boot/driver");
  args.driver_runner_root_driver_url = "fuchsia-boot:///#meta/platform_bus2.cm";

  devmgr_integration_test::IsolatedDevmgr devmgr;
  ASSERT_OK(devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr));
}
