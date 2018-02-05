// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings2/interface_handle.h"

#include "lib/fidl/cpp/bindings2/test/frobinator.h"
#include "lib/fidl/cpp/test/loop_config.h"

namespace fidl {
namespace {

TEST(InterfaceHandle, Trivial) {
  InterfaceHandle<test::Frobinator> handle;
}

TEST(InterfaceHandle, InterfacePtrConversion) {
  async::Loop loop(&kTestLoopConfig);

  test::FrobinatorPtr ptr;
  auto request = ptr.NewRequest();
  EXPECT_TRUE(request.is_valid());
  EXPECT_TRUE(ptr.is_bound());

  InterfaceHandle<test::Frobinator> handle = std::move(ptr);
  EXPECT_TRUE(handle.is_valid());
  EXPECT_FALSE(ptr.is_bound());

  handle = nullptr;
  EXPECT_FALSE(handle.is_valid());
}

TEST(InterfaceHandle, NewRequest) {
  async::Loop loop(&kTestLoopConfig);

  InterfaceHandle<test::Frobinator> handle;
  EXPECT_FALSE(handle.is_valid());
  auto request = handle.NewRequest();
  EXPECT_TRUE(request.is_valid());
  EXPECT_TRUE(handle.is_valid());
  test::FrobinatorPtr ptr = handle.Bind();
  EXPECT_FALSE(handle.is_valid());
  EXPECT_TRUE(ptr.is_bound());
}

TEST(InterfaceHandle, Channel) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx_handle_t saved = h1.get();
  InterfaceHandle<test::Frobinator> handle(std::move(h1));
  EXPECT_TRUE(handle.is_valid());
  EXPECT_EQ(saved, handle.channel().get());
  zx::channel h3 = handle.TakeChannel();
  EXPECT_EQ(saved, h3.get());
  handle.set_channel(std::move(h3));
  EXPECT_EQ(saved, handle.channel().get());
}

TEST(InterfaceHandle, PutAt) {
  uint8_t buffer[1024];
  Builder builder(buffer, sizeof(buffer));

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx_handle_t saved = h1.get();
  InterfaceHandle<test::Frobinator> handle(std::move(h1));
  zx_handle_t* view = builder.New<ViewOf<decltype(handle)>::type>();
  EXPECT_EQ(ZX_HANDLE_INVALID, *view);
  EXPECT_TRUE(PutAt(&builder, view, &handle));
  EXPECT_EQ(ZX_HANDLE_INVALID, h1.get());
  EXPECT_EQ(saved, *view);
  EXPECT_EQ(ZX_OK, zx_handle_close(*view));
}

}  // namespace
}  // namespace fidl
