// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

namespace {

// Returns zero on failure.
static zx_rights_t get_vmo_rights(const zx::vmo& vmo) {
  zx_info_handle_basic_t info;
  zx_status_t s =
      zx_object_get_info(vmo.get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (s != ZX_OK) {
    EXPECT_EQ(s, ZX_OK);  // Poison the test
    return 0;
  }
  return info.rights;
}

TEST_F(NamespaceTest, HasAmbientExecutable) {
  zx::vmo vmo, vmo2, vmo3;

  // allocate an object
  ASSERT_EQ(ZX_OK, zx::vmo::create(PAGE_SIZE, 0, &vmo));

  // set-exec with an invalid VMEX resource handle
  ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_READ, &vmo2));
  ASSERT_EQ(ZX_OK, vmo2.replace_as_executable(zx::resource(), &vmo3));
  EXPECT_EQ(ZX_RIGHT_READ | ZX_RIGHT_EXECUTE, get_vmo_rights(vmo3));
}

}  // namespace
