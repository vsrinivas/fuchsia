// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/unique_handle.h"
#include "gtest/gtest.h"

namespace mtl {
namespace {

TEST(UniqueHandle, Control) {
  UniqueHandle handle;
  EXPECT_FALSE(handle.is_valid());
  EXPECT_FALSE(handle.is_error());

  handle = UniqueHandle(ERR_IO);
  EXPECT_FALSE(handle.is_valid());
  EXPECT_TRUE(handle.is_error());
}

}  // namespace
}  // namespace mtl
