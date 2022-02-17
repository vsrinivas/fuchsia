// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "guest_test.h"

using VirtioSoundGuestTest = GuestTest<TerminaEnclosedGuest>;

TEST_F(VirtioSoundGuestTest, Placeholder) {
  std::string text;
  int32_t return_code = 0;
  ASSERT_EQ(this->Execute({"/tmp/vm_extras/aplay", "--version"}, &text, &return_code), ZX_OK);
  EXPECT_EQ(return_code, 0) << "[BEGIN GUEST TEXT]" << text << "[END GUEST TEXT]";
}
