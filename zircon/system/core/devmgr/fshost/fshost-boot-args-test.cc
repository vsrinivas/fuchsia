// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fshost-boot-args.h"

#include <lib/boot-args/boot-args.h>
#include <zircon/assert.h>

#include <zxtest/zxtest.h>

// Create a subclass to access the protected test-only constructor on
// FshostBootArgs.
class FshostBootArgsForTest : public devmgr::FshostBootArgs {
 public:
  FshostBootArgsForTest(std::unique_ptr<devmgr::BootArgs> boot_args)
      : FshostBootArgs(std::move(boot_args)) {}
};

std::unique_ptr<FshostBootArgsForTest> CreateFshostBootArgs(const char* config, size_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  EXPECT_OK(status);

  status = vmo.write(config, 0, size);
  EXPECT_OK(status);

  auto boot_args = std::make_unique<devmgr::BootArgs>();
  status = devmgr::BootArgs::Create(std::move(vmo), size, boot_args.get());
  EXPECT_OK(status);

  return std::make_unique<FshostBootArgsForTest>(std::move(boot_args));
}

TEST(FshostBootArgsTest, GetDefaultBools) {
  const char config[] = "";
  auto boot_args = CreateFshostBootArgs(config, sizeof(config));

  ASSERT_EQ(false, boot_args->netboot());
  ASSERT_EQ(false, boot_args->check_filesystems());
  ASSERT_EQ(true, boot_args->wait_for_data());
}

TEST(FshostBootArgsTest, GetNonDefaultBools) {
  const char config[] =
      "netsvc.netboot"
      "\0zircon.system.disable-automount"
      "\0zircon.system.filesystem-check"
      "\0zircon.system.wait-for-data=false";
  auto boot_args = CreateFshostBootArgs(config, sizeof(config));

  ASSERT_EQ(true, boot_args->netboot());
  ASSERT_EQ(true, boot_args->check_filesystems());
  ASSERT_EQ(false, boot_args->wait_for_data());
}

TEST(FshostBootArgsTest, GetPkgfsFile) {
  const char config[] =
      "zircon.system.pkgfs.file.foobar=aaa"
      "\0zircon.system.pkgfs.file.foobaz=bbb"
      "\0zircon.system.pkgfs.file.111=ccc"
      "\0zircon.system.pkgfs.file.222=ddd";
  auto boot_args = CreateFshostBootArgs(config, sizeof(config));

  ASSERT_STR_EQ("aaa", boot_args->pkgfs_file_with_prefix_and_name("foo", "bar"));
  ASSERT_STR_EQ("bbb", boot_args->pkgfs_file_with_prefix_and_name("foo", "baz"));
  ASSERT_STR_EQ("ccc", boot_args->pkgfs_file_with_prefix_and_name("111", ""));
  ASSERT_STR_EQ("ddd", boot_args->pkgfs_file_with_prefix_and_name("", "222"));
}

TEST(FshostBootArgsTest, GetPkgfsCmd) {
  const char config[] = "zircon.system.pkgfs.cmd=foobar";
  auto boot_args = CreateFshostBootArgs(config, sizeof(config));

  ASSERT_STR_EQ("foobar", boot_args->pkgfs_cmd());
}
