// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "boot-args.h"

#include <zxtest/zxtest.h>

namespace {
void CreateBootArgs(const char* config, size_t size, devmgr::BootArgs* boot_args) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  ASSERT_OK(status);

  status = vmo.write(config, 0, size);
  ASSERT_OK(status);

  status = devmgr::BootArgs::Create(std::move(vmo), size, boot_args);
  ASSERT_OK(status);
}

TEST(BootArgsTestCase, CreateZeroSized) {
  devmgr::BootArgs boot_args;
  ASSERT_NO_FATAL_FAILURES(CreateBootArgs("", 0, &boot_args));
}

TEST(BootArgsTestCase, Get) {
  const char config[] = "key1=old-value\0key2=value2\0key1=new-value";

  devmgr::BootArgs boot_args;
  ASSERT_NO_FATAL_FAILURES(CreateBootArgs(config, sizeof(config), &boot_args));
  ASSERT_STR_EQ("new-value", boot_args.Get("key1"));
  ASSERT_STR_EQ("value2", boot_args.Get("key2"));
}

TEST(BootArgsTestCase, GetBool) {
  const char config[] = "key1\0key2=hello\0key3=false\0key4=off\0key5=0";

  devmgr::BootArgs boot_args;
  ASSERT_NO_FATAL_FAILURES(CreateBootArgs(config, sizeof(config), &boot_args));
  ASSERT_TRUE(boot_args.GetBool("key1", false));
  ASSERT_TRUE(boot_args.GetBool("key2", false));
  ASSERT_TRUE(boot_args.GetBool("missing", true));
  ASSERT_FALSE(boot_args.GetBool("key3", false));
  ASSERT_FALSE(boot_args.GetBool("key4", false));
  ASSERT_FALSE(boot_args.GetBool("key5", false));
}

TEST(BootArgsTestCase, Collect) {
  const char config[] = "key1\0key2=value2\0key3=value3\0yek=eulav";

  devmgr::BootArgs boot_args;
  ASSERT_NO_FATAL_FAILURES(CreateBootArgs(config, sizeof(config), &boot_args));
  fbl::Vector<const char*> out;
  boot_args.Collect("key", &out);
  ASSERT_EQ(3, out.size());
  ASSERT_STR_EQ("key1", out[0]);
  ASSERT_STR_EQ("key2=value2", out[1]);
  ASSERT_STR_EQ("key3=value3", out[2]);
}
}  // namespace
