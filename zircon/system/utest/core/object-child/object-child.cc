// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/process.h>
#include <zircon/errors.h>
#include <zircon/rights.h>

#include <zxtest/zxtest.h>

namespace {

// zx_object_get_child(ZX_HANDLE_INVALID) should return
// ZX_ERR_BAD_HANDLE. fxbug.dev/31574
TEST(ObjectChildTest, InvalidHandleReturnsBadHandle) {
  zx::unowned_process self = zx::process::self();
  zx_info_handle_basic_t info;
  zx_handle_t process;

  ASSERT_OK(self->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));

  EXPECT_EQ(zx_object_get_child(ZX_HANDLE_INVALID, info.koid, ZX_RIGHT_SAME_RIGHTS, &process),
            ZX_ERR_BAD_HANDLE);
}

}  // namespace
