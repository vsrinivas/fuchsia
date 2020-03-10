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

// Create a subclass to access the protected test-only constructor on
// FshostBootArgs.
class FshostBootArgsForTest : public devmgr::FshostBootArgs {
 public:
  explicit FshostBootArgsForTest(
      std::unique_ptr<llcpp::fuchsia::boot::Arguments::SyncClient>&& boot_args)
      : FshostBootArgs(std::move(boot_args)) {}
};

std::unique_ptr<FshostBootArgsForTest> CreateFshostBootArgs(async_dispatcher_t* dispatcher,
                                                            mock_boot_arguments::Server& server) {
  std::unique_ptr<llcpp::fuchsia::boot::Arguments::SyncClient> client =
      std::make_unique<llcpp::fuchsia::boot::Arguments::SyncClient>(zx::channel());
  server.CreateClient(dispatcher, client.get());
  return std::make_unique<FshostBootArgsForTest>(std::move(client));
}

TEST(FshostBootArgsTest, GetDefaultBools) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  auto server = mock_boot_arguments::Server{{}};
  auto boot_args = CreateFshostBootArgs(loop.dispatcher(), server);

  ASSERT_EQ(false, boot_args->netboot());
  ASSERT_EQ(false, boot_args->check_filesystems());
  ASSERT_EQ(true, boot_args->wait_for_data());
  ASSERT_EQ(false, boot_args->blobfs_enable_userpager());
  ASSERT_EQ(false, boot_args->blobfs_write_uncompressed());
}

TEST(FshostBootArgsTest, GetNonDefaultBools) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  std::map<std::string, std::string> config = {
      {"netsvc.netboot", ""},
      {"zircon.system.disable-automount", ""},
      {"zircon.system.filesystem-check", ""},
      {"zircon.system.wait-for-data", "false"},
      {"blobfs.userpager", ""},
      {"blobfs.uncompressed", ""},
  };
  auto server = mock_boot_arguments::Server{std::move(config)};
  auto boot_args = CreateFshostBootArgs(loop.dispatcher(), server);

  ASSERT_EQ(true, boot_args->netboot());
  ASSERT_EQ(true, boot_args->check_filesystems());
  ASSERT_EQ(false, boot_args->wait_for_data());
  ASSERT_EQ(true, boot_args->blobfs_enable_userpager());
  ASSERT_EQ(true, boot_args->blobfs_write_uncompressed());
}

TEST(FshostBootArgsTest, GetPkgfsFile) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  std::map<std::string, std::string> config = {
      {"zircon.system.pkgfs.file.foobar", "aaa"},
      {"zircon.system.pkgfs.file.foobaz", "bbb"},
      {"zircon.system.pkgfs.file.111", "ccc"},
      {"zircon.system.pkgfs.file.222", "ddd"},
  };
  auto server = mock_boot_arguments::Server{std::move(config)};
  auto boot_args = CreateFshostBootArgs(loop.dispatcher(), server);

  ASSERT_STR_EQ("aaa", *boot_args->pkgfs_file_with_prefix_and_name("foo", "bar"));
  ASSERT_STR_EQ("bbb", *boot_args->pkgfs_file_with_prefix_and_name("foo", "baz"));
  ASSERT_STR_EQ("ccc", *boot_args->pkgfs_file_with_prefix_and_name("111", ""));
  ASSERT_STR_EQ("ddd", *boot_args->pkgfs_file_with_prefix_and_name("", "222"));
}

TEST(FshostBootArgsTest, GetPkgfsCmd) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  std::map<std::string, std::string> config = {{"zircon.system.pkgfs.cmd", "foobar"}};
  auto server = mock_boot_arguments::Server{std::move(config)};
  auto boot_args = CreateFshostBootArgs(loop.dispatcher(), server);

  ASSERT_STR_EQ("foobar", *boot_args->pkgfs_cmd());
}
