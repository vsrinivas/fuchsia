// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/zx/vmo.h>
#include <zircon/rights.h>
#include <zxtest/zxtest.h>

#include <climits> // PAGE_SIZE
#include <utility>

namespace {

static constexpr size_t kVmoTestSize = 512 << 10; // 512KB
static constexpr uint32_t kPageCount = kVmoTestSize / PAGE_SIZE;

TEST(FakeBti, CreateFakeBti) {
    zx_handle_t bti = ZX_HANDLE_INVALID;
    EXPECT_OK(fake_bti_create(&bti));
    EXPECT_NE(bti, ZX_HANDLE_INVALID);
    ASSERT_NO_DEATH(([bti]() { fake_bti_destroy(bti); }));
}

TEST(FakeBti, PinVmo) {
    zx_handle_t bti = ZX_HANDLE_INVALID;
    EXPECT_OK(fake_bti_create(&bti));
    EXPECT_NE(bti, ZX_HANDLE_INVALID);

    zx_handle_t vmo_handle, pmt_handle;
    EXPECT_OK(zx_vmo_create(kVmoTestSize, 0, &vmo_handle));

    zx_paddr_t addrs[kPageCount];

    // Now actually pin the region
    EXPECT_OK(zx_bti_pin(bti, 0, vmo_handle, 0, kVmoTestSize, addrs,
                         kPageCount, &pmt_handle));
    EXPECT_NE(pmt_handle, ZX_HANDLE_INVALID);

    ASSERT_NO_DEATH(([pmt_handle]() { EXPECT_OK(zx_pmt_unpin(pmt_handle)); }));
    ASSERT_NO_DEATH(([bti]() { fake_bti_destroy(bti); }));
}

TEST(FakeBti, CreateContiguousVmo) {
    zx_handle_t bti = ZX_HANDLE_INVALID;
    EXPECT_OK(fake_bti_create(&bti));
    EXPECT_NE(bti, ZX_HANDLE_INVALID);

    zx_handle_t vmo_handle, pmt_handle;
    EXPECT_OK(zx_vmo_create_contiguous(bti, kVmoTestSize, 0, &vmo_handle));
    EXPECT_NE(vmo_handle, ZX_HANDLE_INVALID);

    zx_paddr_t addr;
    EXPECT_OK(zx_bti_pin(bti, ZX_BTI_CONTIGUOUS, vmo_handle, 0, kVmoTestSize,
                         &addr, 1, &pmt_handle));
    EXPECT_NE(pmt_handle, ZX_HANDLE_INVALID);

    ASSERT_NO_DEATH(([pmt_handle]() { EXPECT_OK(zx_pmt_unpin(pmt_handle)); }));
    ASSERT_NO_DEATH(([bti]() { fake_bti_destroy(bti); }));
}

// TODO(ZX-3131): when functionality is available, check that pinning a
// vmo with the ZX_BTI_CONTIGUOUS flag fails if the vmo was not created with
// zx_vmo_create_contiguous.

} // namespace
