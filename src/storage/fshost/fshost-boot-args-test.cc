// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fshost-boot-args.h"

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <zircon/assert.h>

#include <map>
#include <memory>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

// Create a subclass to access the test-only constructor on FshostBootArgs.
class FshostBootArgsForTest : public devmgr::FshostBootArgs {
 public:
  explicit FshostBootArgsForTest(llcpp::fuchsia::boot::Arguments::SyncClient boot_args)
      : FshostBootArgs(std::move(boot_args)) {}
};

class FshostBootArgsTest : public zxtest::Test {
 public:
  FshostBootArgsTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void CreateFshostBootArgs(std::map<std::string, std::string> config) {
    boot_args_server_ = mock_boot_arguments::Server{std::move(config)};
    llcpp::fuchsia::boot::Arguments::SyncClient client;
    boot_args_server_.CreateClient(loop_.dispatcher(), &client);

    ASSERT_OK(loop_.StartThread());
    boot_args_ = std::make_unique<FshostBootArgsForTest>(std::move(client));
  }

  FshostBootArgsForTest& boot_args() { return *boot_args_; }

 private:
  async::Loop loop_;
  mock_boot_arguments::Server boot_args_server_;
  std::unique_ptr<FshostBootArgsForTest> boot_args_;
};

TEST_F(FshostBootArgsTest, GetDefaultBools) {
  ASSERT_NO_FATAL_FAILURES(CreateFshostBootArgs({}));

  EXPECT_EQ(false, boot_args().netboot());
  EXPECT_EQ(false, boot_args().check_filesystems());
  EXPECT_EQ(true, boot_args().wait_for_data());
  EXPECT_EQ(false, boot_args().blobfs_enable_userpager());
}

TEST_F(FshostBootArgsTest, GetNonDefaultBools) {
  std::map<std::string, std::string> config = {
      {"netsvc.netboot", ""},
      {"zircon.system.disable-automount", ""},
      {"zircon.system.filesystem-check", ""},
      {"zircon.system.wait-for-data", "false"},
      {"blobfs.userpager", ""},
  };
  ASSERT_NO_FATAL_FAILURES(CreateFshostBootArgs(config));

  EXPECT_EQ(true, boot_args().netboot());
  EXPECT_EQ(true, boot_args().check_filesystems());
  EXPECT_EQ(false, boot_args().wait_for_data());
  EXPECT_EQ(true, boot_args().blobfs_enable_userpager());
}

TEST_F(FshostBootArgsTest, GetPkgfsFile) {
  std::map<std::string, std::string> config = {
      {"zircon.system.pkgfs.file.foobar", "aaa"},
      {"zircon.system.pkgfs.file.bin/foobaz", "bbb"},
      {"zircon.system.pkgfs.file.lib/foobar", "ccc"},
  };
  ASSERT_NO_FATAL_FAILURES(CreateFshostBootArgs(config));

  EXPECT_EQ("aaa", boot_args().pkgfs_file_with_path("foobar"));
  EXPECT_EQ("bbb", boot_args().pkgfs_file_with_path("bin/foobaz"));
  EXPECT_EQ("ccc", boot_args().pkgfs_file_with_path("lib/foobar"));
}

TEST_F(FshostBootArgsTest, GetPkgfsCmd) {
  std::map<std::string, std::string> config = {{"zircon.system.pkgfs.cmd", "foobar"}};
  ASSERT_NO_FATAL_FAILURES(CreateFshostBootArgs(config));

  EXPECT_EQ("foobar", boot_args().pkgfs_cmd());
}

TEST_F(FshostBootArgsTest, GetBlobfsCompressionAlgorithm) {
  std::map<std::string, std::string> config = {{"blobfs.write-compression-algorithm", "ZSTD"}};
  ASSERT_NO_FATAL_FAILURES(CreateFshostBootArgs(config));

  EXPECT_EQ("ZSTD", boot_args().blobfs_write_compression_algorithm());
}

TEST_F(FshostBootArgsTest, GetBlobfsCompressionAlgorithm_Unspecified) {
  ASSERT_NO_FATAL_FAILURES(CreateFshostBootArgs({}));

  EXPECT_EQ(std::nullopt, boot_args().blobfs_write_compression_algorithm());
}
