// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/handles/unique_handle.h"
#include "gtest/gtest.h"

namespace mtl {
namespace {

TEST(UniqueHandle, Control) {
  UniqueHandle handle;
  EXPECT_FALSE(handle.is_valid());

  handle = UniqueHandle(4);
  EXPECT_TRUE(handle.is_valid());
  // Release the handle to avoid closing handle 4, which might be a real handle
  // that's used by something.
  (void)handle.release();

  handle = UniqueHandle(ERR_IO);
  EXPECT_FALSE(handle.is_valid());
}

}  // namespace
}  // namespace mtl
