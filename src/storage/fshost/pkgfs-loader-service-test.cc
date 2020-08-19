// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/pkgfs-loader-service.h"

#include <zircon/errors.h>

#include <gtest/gtest.h>
#include <mock-boot-arguments/server.h>

#include "src/lib/loader_service/loader_service_test_fixture.h"

namespace devmgr {
namespace {

using namespace loader::test;

namespace fldsvc = ::llcpp::fuchsia::ldsvc;

// Create a subclass to access the test-only constructor on FshostBootArgs.
class FshostBootArgsForTest : public FshostBootArgs {
 public:
  explicit FshostBootArgsForTest(llcpp::fuchsia::boot::Arguments::SyncClient boot_args)
      : FshostBootArgs(std::move(boot_args)) {}
};

class PkgfsLoaderServiceTest : public LoaderServiceTest {
 public:
  void CreateTestLoader(std::vector<TestDirectoryEntry> blobfs_config,
                        std::map<std::string, std::string> boot_args_config,
                        std::shared_ptr<PkgfsLoaderService>* loader) {
    fbl::unique_fd blobfs_fd;
    ASSERT_NO_FATAL_FAILURE(CreateTestDirectory(std::move(blobfs_config), &blobfs_fd));

    boot_args_server_ = mock_boot_arguments::Server{std::move(boot_args_config)};
    llcpp::fuchsia::boot::Arguments::SyncClient client;
    // We can run the mock boot args server on the same loop as the memfs directory, since they
    // don't interact. The loop was already started by CreateTestDirectory.
    boot_args_server_.CreateClient(fs_loop().dispatcher(), &client);
    auto boot_args = std::make_shared<FshostBootArgsForTest>(std::move(client));

    *loader = PkgfsLoaderService::Create(std::move(blobfs_fd), std::move(boot_args));
  }

 private:
  mock_boot_arguments::Server boot_args_server_;
};

TEST_F(PkgfsLoaderServiceTest, LoadObject) {
  std::shared_ptr<PkgfsLoaderService> loader;
  std::vector<TestDirectoryEntry> bootfs_config = {
      {"abc", "foo", true},
      {"123", "asan foo", true},
      {"not_exec", "foo", false},
      {"pkgfs_blob", "pkgfs", true},
  };
  std::map<std::string, std::string> boot_args_config = {
      {"zircon.system.pkgfs.file.lib/libfoo.so", "abc"},
      {"zircon.system.pkgfs.file.lib/asan/libfoo.so", "123"},
      {"zircon.system.pkgfs.file.lib/no_blob", "no_blob"},
      {"zircon.system.pkgfs.file.lib/not_exec", "not_exec"},
      {"zircon.system.pkgfs.file.bin/pkgfs", "pkgfs_blob"},
  };
  ASSERT_NO_FATAL_FAILURE(
      CreateTestLoader(std::move(bootfs_config), std::move(boot_args_config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "no_arg", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "no_blob", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "not_exec", zx::error(ZX_ERR_ACCESS_DENIED)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "pkgfs", zx::error(ZX_ERR_NOT_FOUND)));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("foo")));
  ASSERT_NO_FATAL_FAILURE(Config(client, "asan", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("asan foo")));
}

TEST_F(PkgfsLoaderServiceTest, LoadPkgfsFile) {
  std::shared_ptr<PkgfsLoaderService> loader;
  std::vector<TestDirectoryEntry> bootfs_config = {
      {"pkgfs_blob", "pkgfs", true},
  };
  std::map<std::string, std::string> boot_args_config = {
      {"zircon.system.pkgfs.file.bin/pkgfs", "pkgfs_blob"},
  };
  ASSERT_NO_FATAL_FAILURE(
      CreateTestLoader(std::move(bootfs_config), std::move(boot_args_config), &loader));

  ASSERT_TRUE(loader->LoadPkgfsFile("bin/pkgfs").is_ok());
  ASSERT_TRUE(loader->LoadPkgfsFile("bin/otherfs").is_error());
}

}  // namespace
}  // namespace devmgr
