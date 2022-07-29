// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

TEST(LdsoXOM, Test) {
  int a(void);
  EXPECT_EQ(a(), 2);
}
