// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/deprecated-loader-service.h"

#include "src/lib/loader_service/loader_service_test_fixture.h"

namespace loader {
namespace test {
namespace {

namespace fldsvc = ::llcpp::fuchsia::ldsvc;

TEST_F(LoaderServiceTest, SystemBeforeBoot) {
  std::shared_ptr<DeprecatedBootSystemLoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("boot/lib/libfoo.so", "boot", true);
  config.emplace_back("boot/lib/libboot.so", "boot", true);
  config.emplace_back("system/lib/libfoo.so", "system", true);
  config.emplace_back("system/lib/libsystem.so", "system", true);
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libboot.so", zx::ok("boot")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libsystem.so", zx::ok("system")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("system")));
}

// The system directory has no contents initially, and then is mounted over and becomes populated
// after the loader is already in use.
TEST_F(LoaderServiceTest, SystemDelayedMount) {
  std::shared_ptr<DeprecatedBootSystemLoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("boot/lib/libfoo.so", "boot", true);
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("boot")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libsystem.so", zx::error(ZX_ERR_NOT_FOUND)));

  ASSERT_NO_FATAL_FAILURE(
      AddDirectoryEntry(root_dir(), TestDirectoryEntry{"system/lib/libfoo.so", "system", true}));
  ASSERT_NO_FATAL_FAILURE(
      AddDirectoryEntry(root_dir(), TestDirectoryEntry{"system/lib/libsystem.so", "system", true}));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("system")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libsystem.so", zx::ok("system")));
}

}  // namespace
}  // namespace test
}  // namespace loader
