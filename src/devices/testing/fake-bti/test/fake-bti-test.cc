// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>
#include <zircon/rights.h>

#include <climits>  // PAGE_SIZE
#include <utility>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

namespace {

static constexpr size_t kVmoTestSize = 512 << 10;  // 512KB
static constexpr uint32_t kPageCount = kVmoTestSize / PAGE_SIZE;

TEST(FakeBti, CreateFakeBti) {
  zx_handle_t bti = ZX_HANDLE_INVALID;
  EXPECT_OK(fake_bti_create(&bti));
  EXPECT_NE(bti, ZX_HANDLE_INVALID);
  ASSERT_NO_DEATH(([bti]() { zx_handle_close(bti); }));
}

TEST(FakeBti, PinVmo) {
  zx_handle_t bti = ZX_HANDLE_INVALID;
  EXPECT_OK(fake_bti_create(&bti));
  EXPECT_NE(bti, ZX_HANDLE_INVALID);

  zx_handle_t vmo_handle, pmt_handle;
  EXPECT_OK(zx_vmo_create(kVmoTestSize, 0, &vmo_handle));

  // Create an address array with one extra entry and mark it with a sentinel value.
  zx_paddr_t addrs[kPageCount + 1];
  addrs[kPageCount] = 42;

  // Now actually pin the region
  EXPECT_OK(zx_bti_pin(bti, 0, vmo_handle, 0, kVmoTestSize, addrs, kPageCount, &pmt_handle));
  EXPECT_NE(pmt_handle, ZX_HANDLE_INVALID);

  // Check that the addresses returned are correct, including that the sentinel value wasn't
  // touched.
  for (size_t i = 0; i != kPageCount; ++i) {
    EXPECT_EQ(addrs[i], FAKE_BTI_PHYS_ADDR);
  }
  EXPECT_EQ(addrs[kPageCount], 42);

  ASSERT_NO_DEATH(([pmt_handle]() { EXPECT_OK(zx_pmt_unpin(pmt_handle)); }));
  ASSERT_NO_DEATH(([bti]() { zx_handle_close(bti); }));
}

TEST(FakeBti, CreateContiguousVmo) {
  zx_handle_t bti = ZX_HANDLE_INVALID;
  EXPECT_OK(fake_bti_create(&bti));
  EXPECT_NE(bti, ZX_HANDLE_INVALID);

  zx_handle_t vmo_handle, pmt_handle;
  EXPECT_OK(zx_vmo_create_contiguous(bti, kVmoTestSize, 0, &vmo_handle));
  EXPECT_NE(vmo_handle, ZX_HANDLE_INVALID);

  zx_paddr_t addr;
  EXPECT_OK(zx_bti_pin(bti, ZX_BTI_CONTIGUOUS, vmo_handle, 0, kVmoTestSize, &addr, 1, &pmt_handle));
  EXPECT_NE(pmt_handle, ZX_HANDLE_INVALID);
  EXPECT_EQ(addr, FAKE_BTI_PHYS_ADDR);

  ASSERT_NO_DEATH(([pmt_handle]() { EXPECT_OK(zx_pmt_unpin(pmt_handle)); }));
  ASSERT_NO_DEATH(([bti]() { zx_handle_close(bti); }));
}

// TODO(ZX-3131): when functionality is available, check that pinning a
// vmo with the ZX_BTI_CONTIGUOUS flag fails if the vmo was not created with
// zx_vmo_create_contiguous.

}  // namespace
