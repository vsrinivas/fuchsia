// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/zx/vmo.h>
#include <zircon/rights.h>

#include <utility>

#include <zxtest/zxtest.h>

namespace {

constexpr size_t kVmoTestSize = 512 << 10;  // 512KB

class PinnedVmoTester : public zxtest::Test {
 public:
  void Init() { ASSERT_OK(zx::vmo::create(kVmoTestSize, 0, &vmo_)); }

  void InitContiguous() {
    ASSERT_EQ(zx::vmo::create_contiguous(bti_, kVmoTestSize, 0, &vmo_), ZX_OK);
  }

  void Pin(uint32_t rights) {
    // Make sure our handles are valid:
    EXPECT_TRUE(vmo_.is_valid());
    EXPECT_OK(pinned_vmo_.Pin(vmo_, bti_, rights));
  }

  // Check that the PinnedVmo is pinned; that it has regions
  // and the regions are of non-zero size
  void CheckPinned() const {
    uint32_t region_count = pinned_vmo_.region_count();
    ASSERT_GT(region_count, 0);
    for (uint32_t i = 0; i < region_count; ++i) {
      fzl::PinnedVmo::Region r = pinned_vmo_.region(i);
      EXPECT_GT(r.size, 0);
      // We would check that phys_addr != 0, but fake-bti sets all the
      // addresses to zero.
    }
  }

  // Check that the PinnedVmo is pinned; that it has only one region
  // and the region is of non-zero size
  void CheckContiguousPinned() const {
    ASSERT_EQ(pinned_vmo_.region_count(), 1);
    CheckPinned();
  }

  void CheckUnpinned() const { ASSERT_EQ(pinned_vmo_.region_count(), 0); }

  void SetUp() override { ASSERT_OK(fake_bti_create(bti_.reset_and_get_address())); }

  ~PinnedVmoTester() override { pinned_vmo_.Unpin(); }

  zx::bti bti_;
  zx::vmo vmo_;
  fzl::PinnedVmo pinned_vmo_;
};

TEST_F(PinnedVmoTester, CreateAndPinTest) {
  CheckUnpinned();
  Init();
  Pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE);
  CheckPinned();
}

TEST_F(PinnedVmoTester, CreateContiguousTest) {
  CheckUnpinned();
  InitContiguous();
  Pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS);
  CheckContiguousPinned();
}

TEST_F(PinnedVmoTester, FailPinTwiceTest) {
  Init();
  Pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE);
  EXPECT_EQ(pinned_vmo_.Pin(vmo_, bti_, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE), ZX_ERR_BAD_STATE);
}

TEST(PinnedVmoTests, FailPinArgsTest) {
  fzl::PinnedVmo pinned_vmo;
  zx::vmo vmo;
  zx::bti bti;
  EXPECT_EQ(pinned_vmo.Pin(vmo, bti, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE), ZX_ERR_INVALID_ARGS);
  ASSERT_OK(zx::vmo::create(kVmoTestSize, 0, &vmo));
  EXPECT_EQ(pinned_vmo.Pin(vmo, bti, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE), ZX_ERR_INVALID_ARGS);
}

TEST_F(PinnedVmoTester, PinRangeTest) {
  CheckUnpinned();
  Init();
  EXPECT_OK(pinned_vmo_.PinRange(ZX_PAGE_SIZE, ZX_PAGE_SIZE * 4, vmo_, *zx::unowned_bti(bti_),
                                 ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE));
  EXPECT_EQ(pinned_vmo_.region_count(), 4);
  ASSERT_NO_FATAL_FAILURES(CheckPinned());
}

TEST_F(PinnedVmoTester, FailPinRangeTest) {
  CheckUnpinned();
  Init();
  constexpr uint32_t options = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE;
  // offset not page aligned:
  EXPECT_EQ(
      pinned_vmo_.PinRange(ZX_PAGE_SIZE + 1, ZX_PAGE_SIZE, vmo_, *zx::unowned_bti(bti_), options),
      ZX_ERR_INVALID_ARGS);
  // length not page aligned:
  EXPECT_EQ(pinned_vmo_.PinRange(0, ZX_PAGE_SIZE + 1, vmo_, *zx::unowned_bti(bti_), options),
            ZX_ERR_INVALID_ARGS);
  // zero length:
  EXPECT_EQ(pinned_vmo_.PinRange(0, 0, vmo_, *zx::unowned_bti(bti_), options), ZX_ERR_INVALID_ARGS);
}

}  // namespace
