// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls-next.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

#include "test_thread.h"
#include "userpager.h"

namespace pager_tests {

// Tests that a VMO created with TRAP_DIRTY can be supplied, and generates VMO_DIRTY requests when
// written to.
VMO_VMAR_TEST(SimpleTrapDirty) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));

  TestThread t1([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });
  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  // Supply the page first and then attempt to write to it.
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));
  ASSERT_TRUE(t1.Wait());

  TestThread t2([vmo]() -> bool {
    uint8_t data = 0x77;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.WaitForBlocked());

  // No mechanism to resolve the dirty request for now. Nothing more to do beyond verifying the
  // generation of the request.
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  // TODO: Remove this once the request can be handled successfully.
  ASSERT_TRUE(pager.FailPages(vmo, 0, 1));
  ASSERT_TRUE(t2.WaitForFailure());

  // Writes to a VMO created without TRAP_DIRTY go through without blocking.
  Vmo* vmo_no_trap;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo_no_trap));
  ASSERT_TRUE(pager.SupplyPages(vmo_no_trap, 0, 1));
  uint8_t data = 0xcc;
  ASSERT_OK(vmo_no_trap->vmo().write(&data, 0, sizeof(data)));

  // Verify that a non pager-backed vmo cannot be created with TRAP_DIRTY.
  zx_handle_t handle;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            zx_vmo_create(zx_system_get_page_size(), ZX_VMO_TRAP_DIRTY, &handle));
}

}  // namespace pager_tests
