// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_host_loader_service.h"

#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "src/lib/loader_service/loader_service_test_fixture.h"

namespace {

using namespace loader::test;

namespace fldsvc = ::llcpp::fuchsia::ldsvc;

TEST_F(LoaderServiceTest, LoadObject) {
  std::shared_ptr<DriverHostLoaderService> loader;
  std::vector<TestDirectoryEntry> config = {
      {"libfdio.so", "fdio", true},
      {"libother.so", "not allowed", true},
      {"asan/libfdio.so", "asan fdio", true},
      {"asan/libother.so", "not allowed", true},
  };
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  // Libraries not in the allowlist should fail to load.
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libother.so", zx::error(ZX_ERR_ACCESS_DENIED)));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfdio.so", zx::ok("fdio")));
  ASSERT_NO_FATAL_FAILURE(Config(client, "asan", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfdio.so", zx::ok("asan fdio")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libother.so", zx::error(ZX_ERR_ACCESS_DENIED)));
}

}  // namespace
