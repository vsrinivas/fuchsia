// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <zxtest/zxtest.h>

TEST(DirectVdsoTest, HasVdso) {
  zx::vmo vdso_vmo(zx_take_startup_handle(PA_HND(PA_VMO_VDSO, 1)));
  ASSERT_TRUE(vdso_vmo.is_valid());

  char name[ZX_MAX_NAME_LEN] = {};
  ASSERT_OK(vdso_vmo.get_property(ZX_PROP_NAME, name, sizeof(name)));
  EXPECT_STREQ("vdso/direct", name);
}
