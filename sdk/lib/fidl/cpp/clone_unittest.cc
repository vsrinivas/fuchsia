// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/clone.h"

#include "gtest/gtest.h"

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#endif

namespace fidl {
namespace {

TEST(Clone, Control) {
  int8_t a = 32;
  int8_t b = 0;
  EXPECT_EQ(ZX_OK, Clone(a, &b));
  EXPECT_EQ(32, b);
}

#ifdef __Fuchsia__
TEST(Clone, Socket) {
  zx::socket h1, h2;
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &h1, &h2));
  zx::socket h;

  EXPECT_EQ(ZX_OK, Clone(h1, &h));
  h.reset();
  EXPECT_EQ(ZX_OK, Clone(h, &h2));
  EXPECT_FALSE(h2.is_valid());
}

TEST(Clone, Channel) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx::channel h;

  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, Clone(h1, &h));
  EXPECT_EQ(ZX_OK, Clone(h, &h2));
  EXPECT_FALSE(h2.is_valid());
}
#endif

}  // namespace
}  // namespace fidl
