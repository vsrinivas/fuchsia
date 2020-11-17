// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/interface_handle.h"

#include <lib/zx/channel.h>

#include <fidl/test/frobinator/cpp/fidl.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/test/async_loop_for_test.h"

namespace fidl {
namespace {

TEST(InterfaceHandle, Trivial) { InterfaceHandle<fidl::test::frobinator::Frobinator> handle; }

TEST(InterfaceHandle, InterfacePtrConversion) {
  fidl::test::AsyncLoopForTest loop;

  fidl::test::frobinator::FrobinatorPtr ptr;
  auto request = ptr.NewRequest();
  EXPECT_TRUE(request.is_valid());
  EXPECT_TRUE(ptr.is_bound());

  InterfaceHandle<fidl::test::frobinator::Frobinator> handle = std::move(ptr);
  EXPECT_TRUE(handle.is_valid());
  EXPECT_FALSE(ptr.is_bound());

  handle = nullptr;
  EXPECT_FALSE(handle.is_valid());
}

TEST(InterfaceHandle, NewRequest) {
  fidl::test::AsyncLoopForTest loop;

  InterfaceHandle<fidl::test::frobinator::Frobinator> handle;
  EXPECT_FALSE(handle.is_valid());
  auto request = handle.NewRequest();
  EXPECT_TRUE(request.is_valid());
  EXPECT_TRUE(handle.is_valid());
  fidl::test::frobinator::FrobinatorPtr ptr = handle.Bind();
  EXPECT_FALSE(handle.is_valid());
  EXPECT_TRUE(ptr.is_bound());
}

TEST(InterfaceHandle, Channel) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx_handle_t saved = h1.get();
  InterfaceHandle<fidl::test::frobinator::Frobinator> handle(std::move(h1));
  EXPECT_TRUE(handle.is_valid());
  EXPECT_EQ(saved, handle.channel().get());
  zx::channel h3 = handle.TakeChannel();
  EXPECT_EQ(saved, h3.get());
  handle.set_channel(std::move(h3));
  EXPECT_EQ(saved, handle.channel().get());
}

}  // namespace
}  // namespace fidl
