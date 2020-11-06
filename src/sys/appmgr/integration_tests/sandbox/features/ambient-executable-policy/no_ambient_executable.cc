// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

TEST_F(NamespaceTest, NoAmbientExecutable) {
  zx::vmo vmo, vmo2, vmo3;

  // allocate an object
  ASSERT_EQ(ZX_OK, zx_vmo_create(PAGE_SIZE, 0, vmo.reset_and_get_address()));

  // set-exec with an invalid VMEX resource handle
  ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_READ, &vmo2));
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, vmo2.replace_as_executable(zx::resource(), &vmo3));
  EXPECT_EQ(ZX_HANDLE_INVALID, vmo3);
}
