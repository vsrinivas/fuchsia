// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings2/take.h"

namespace fidl {
namespace {

template<typename T>
void CheckTake() {
  T a = 42;
  T b = 78;
  Take(&a, &b);
  EXPECT_EQ(static_cast<T>(78), a);
  EXPECT_EQ(static_cast<T>(78), b);
}

TEST(Take, Primitive) {
  CheckTake<uint8_t>();
  CheckTake<uint16_t>();
  CheckTake<uint32_t>();
  CheckTake<uint64_t>();
  CheckTake<int8_t>();
  CheckTake<int16_t>();
  CheckTake<int32_t>();
  CheckTake<int64_t>();
}

TEST(Take, Handle) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  EXPECT_NE(ZX_HANDLE_INVALID, h1.get());
  EXPECT_NE(ZX_HANDLE_INVALID, h2.get());

  zx_handle_t view = h1.release();
  zx_handle_t saved = view;
  EXPECT_EQ(ZX_HANDLE_INVALID, h1.get());

  Take(&h1, &view);
  EXPECT_EQ(ZX_HANDLE_INVALID, view);
  EXPECT_NE(ZX_HANDLE_INVALID, h1.get());
  EXPECT_EQ(saved, h1.get());
}

}  // namespace
}  // namespace fidl
