// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "guest_test.h"

static constexpr size_t kVirtioBalloonPageCount = 256;

using VirtioBalloonGuestTest = GuestTest<DebianEnclosedGuest>;

TEST_F(VirtioBalloonGuestTest, VirtioBalloon) {
  std::string result;
  EXPECT_EQ(this->Execute({"echo", "test"}, &result), ZX_OK);
  EXPECT_EQ(result, "test\n");

  fuchsia::virtualization::BalloonControllerSyncPtr balloon_controller;
  ConnectToBalloon(balloon_controller.NewRequest());

  uint32_t initial_num_pages;
  zx_status_t status = balloon_controller->GetNumPages(&initial_num_pages);
  ASSERT_EQ(status, ZX_OK);

  // Request an increase to the number of pages in the balloon.
  status = balloon_controller->RequestNumPages(initial_num_pages + kVirtioBalloonPageCount);
  ASSERT_EQ(status, ZX_OK);

  // Verify that the number of pages eventually equals the requested number. The
  // guest may not respond to the request immediately so we call GetNumPages in
  // a loop.
  uint32_t num_pages;
  while (true) {
    status = balloon_controller->GetNumPages(&num_pages);
    ASSERT_EQ(status, ZX_OK);
    if (num_pages == initial_num_pages + kVirtioBalloonPageCount) {
      break;
    }
  }

  // Request a decrease to the number of pages in the balloon back to the
  // initial value.
  status = balloon_controller->RequestNumPages(initial_num_pages);
  ASSERT_EQ(status, ZX_OK);

  while (true) {
    status = balloon_controller->GetNumPages(&num_pages);
    ASSERT_EQ(status, ZX_OK);
    if (num_pages == initial_num_pages) {
      break;
    }
  }
}
