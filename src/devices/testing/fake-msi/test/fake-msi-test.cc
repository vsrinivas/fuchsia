// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-msi/msi.h>
#include <lib/fake-object/object.h>
#include <lib/zx/event.h>
#include <lib/zx/msi.h>
#include <lib/zx/vmo.h>
#include <zircon/rights.h>

#include <climits>
#include <utility>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

namespace {

class FakeMsiTests : public zxtest::Test {};

// If an MSI allocation goes out of scope before all the interrupts created
// off of it the dtor should assert.
TEST_F(FakeMsiTests, CleanupTest) {
  zx::interrupt interrupt;
  ASSERT_DEATH([&interrupt]() {
    zx::msi msi;
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, /*options=*/0, &vmo));
    ASSERT_OK(vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE));
    ASSERT_OK(zx::msi::allocate(*zx::unowned_resource(), 2, &msi));
    ASSERT_OK(zx::msi::create(msi, /*options=*/0, 0, vmo, /*vmo_offset=*/0, &interrupt));
  });
}

TEST_F(FakeMsiTests, CoreTest) {
  zx::msi msi;
  zx::vmo vmo;
  zx::interrupt int_0, int_1;
  uint32_t msi_cnt = 8;

  // MSI syscalls are expected to use physical VMOs, but can use contiguous, uncached, commit
  zx_info_msi_t msi_info;
  zx_status_t status = zx::msi::allocate(*zx::unowned_resource(ZX_HANDLE_INVALID), msi_cnt, &msi);
  ASSERT_OK(status);
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, /*options=*/0, &vmo));
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr));
  ASSERT_STATUS(zx::msi::create(msi, ZX_INTERRUPT_VIRTUAL, 0, vmo, /*vmo_offset=*/0, &int_0),
                ZX_ERR_INVALID_ARGS);
  ASSERT_OK(vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE));
  // |options| must be zero.
  ASSERT_STATUS(zx::msi::create(msi, ZX_INTERRUPT_VIRTUAL, 0, vmo, /*vmo_offset=*/0, &int_0),
                ZX_ERR_INVALID_ARGS);
  // Bad handle.
  ASSERT_STATUS(zx::msi::create(*zx::unowned_msi(0x123456), /*options=*/0, /*msi_id=*/0, vmo,
                                /*vmo_offset=*/0, &int_0),
                ZX_ERR_BAD_HANDLE);
  // Invalid MSI id.
  ASSERT_STATUS(
      zx::msi::create(msi, /*options=*/0, /*msi_id=*/msi_cnt, vmo, /*vmo_offset=*/0, &int_0),
      ZX_ERR_INVALID_ARGS);
  ASSERT_OK(zx::msi::create(msi, /*options=*/0, /*msi_id=*/0, vmo, /*vmo_offset=*/0, &int_0));
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr));
  ASSERT_EQ(msi_info.interrupt_count, 1);
  // Bad handle (The interrupt handle is real, creeate takes fakes)
  auto res = fake_object::fake_object_create();
  ASSERT_OK(res.status_value());
  ASSERT_STATUS(zx::msi::create(*zx::unowned_msi(res.value()), /*options=*/0, /*msi_id=*/0, vmo,
                                /*vmo_offset=*/0, &int_1),
                ZX_ERR_WRONG_TYPE);
  ASSERT_STATUS(zx::msi::create(msi, /*options=*/0, /*msi_id=*/0, vmo, /*vmo_offset=*/0, &int_1),
                ZX_ERR_ALREADY_BOUND);
  ASSERT_OK(zx::msi::create(msi, /*options=*/0, /*msi_id=*/1, vmo, /*vmo_offset=*/0, &int_1));
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr));
  ASSERT_EQ(msi_info.interrupt_count, 2);
  int_0.reset();
  int_1.reset();
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr));
  ASSERT_EQ(msi_info.interrupt_count, 0);
}

}  // namespace
