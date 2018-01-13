// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings2/put.h"

namespace fidl {
namespace {

template<typename T>
T* CheckPutAt(Builder* builder) {
  T* view = builder->New<T>();
  T value = 42;
  EXPECT_TRUE(PutAt(builder, view, &value));
  return view;
}

TEST(PutAt, Primitive) {
  uint8_t buffer[1024];
  Builder builder(buffer, sizeof(buffer));

  EXPECT_EQ(42u, *CheckPutAt<uint8_t>(&builder));
  EXPECT_EQ(42u, *CheckPutAt<uint16_t>(&builder));
  EXPECT_EQ(42u, *CheckPutAt<uint32_t>(&builder));
  EXPECT_EQ(42u, *CheckPutAt<uint64_t>(&builder));
  EXPECT_EQ(42, *CheckPutAt<int8_t>(&builder));
  EXPECT_EQ(42, *CheckPutAt<int16_t>(&builder));
  EXPECT_EQ(42, *CheckPutAt<int32_t>(&builder));
  EXPECT_EQ(42, *CheckPutAt<int64_t>(&builder));
}

TEST(PutAt, Handle) {
  uint8_t buffer[1024];
  Builder builder(buffer, sizeof(buffer));

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  EXPECT_NE(ZX_HANDLE_INVALID, h1.get());
  EXPECT_NE(ZX_HANDLE_INVALID, h2.get());

  zx_handle_t* view = builder.New<zx_handle_t>();
  EXPECT_EQ(ZX_HANDLE_INVALID, *view);
  EXPECT_TRUE(PutAt(&builder, view, &h1));
  EXPECT_EQ(ZX_HANDLE_INVALID, h1.get());
  EXPECT_NE(ZX_HANDLE_INVALID, *view);
  EXPECT_EQ(ZX_OK, zx_handle_close(*view));
}

}  // namespace
}  // namespace fidl
