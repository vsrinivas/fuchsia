// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-msi/msi.h>
#include <lib/zx/event.h>
#include <lib/zx/msi.h>
#include <lib/zx/vmo.h>
#include <zircon/rights.h>

#include <climits>
#include <utility>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

namespace {

TEST(FakeMsi, CoreTest) {
  zx::msi msi;
  zx::vmo vmo;
  zx::interrupt interrupt, interrupt_dup, i2;
  uint32_t msi_cnt = 8;

  // MSI syscalls are expected to use physical VMOs, but can use contiguous, uncached, commit
  zx_info_msi_t msi_info;
  zx_status_t status = zx::msi::allocate(*zx::unowned_resource(ZX_HANDLE_INVALID), msi_cnt, &msi);
  ASSERT_OK(status);
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, /*options=*/0, &vmo));
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr));
  ASSERT_STATUS(zx::msi::create(msi, ZX_INTERRUPT_VIRTUAL, 0, vmo, /*vmo_offset=*/0, &interrupt),
                ZX_ERR_INVALID_ARGS);
  ASSERT_OK(vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE));
  // |options| must be zero.
  ASSERT_STATUS(zx::msi::create(msi, ZX_INTERRUPT_VIRTUAL, 0, vmo, /*vmo_offset=*/0, &interrupt),
                ZX_ERR_INVALID_ARGS);
  // Bad handle.
  ASSERT_STATUS(zx::msi::create(*zx::unowned_msi(123456), /*options=*/0, /*msi_id=*/0, vmo,
                                /*vmo_offset=*/0, &interrupt),
                ZX_ERR_BAD_HANDLE);
  // Invalid MSI id.
  ASSERT_STATUS(
      zx::msi::create(msi, /*options=*/0, /*msi_id=*/msi_cnt, vmo, /*vmo_offset=*/0, &interrupt),
      ZX_ERR_INVALID_ARGS);
  ASSERT_OK(zx::msi::create(msi, /*options=*/0, /*msi_id=*/0, vmo, /*vmo_offset=*/0, &interrupt));
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr));
  ASSERT_EQ(msi_info.interrupt_count, 1);
  // Wrong handle type
  ASSERT_STATUS(zx::msi::create(*zx::unowned_msi(interrupt.get()), /*options=*/0, /*msi_id=*/0, vmo,
                                /*vmo_offset=*/0, &interrupt_dup),
                ZX_ERR_WRONG_TYPE);
  ASSERT_STATUS(
      zx::msi::create(msi, /*options=*/0, /*msi_id=*/0, vmo, /*vmo_offset=*/0, &interrupt_dup),
      ZX_ERR_ALREADY_BOUND);
  ASSERT_OK(
      zx::msi::create(msi, /*options=*/0, /*msi_id=*/1, vmo, /*vmo_offset=*/0, &interrupt_dup));
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr));
  ASSERT_EQ(msi_info.interrupt_count, 2);
  interrupt.reset();
  interrupt_dup.reset();
  ASSERT_OK(msi.get_info(ZX_INFO_MSI, &msi_info, sizeof(msi_info), nullptr, nullptr));
  ASSERT_EQ(msi_info.interrupt_count, 0);
}

}  // namespace
