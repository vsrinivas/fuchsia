// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/interface_request.h"

#include <lib/zx/channel.h>

#include <utility>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(InterfaceRequest, Trivial) {
  InterfaceRequest<fidl::test::frobinator::Frobinator> request;
}

TEST(InterfaceRequest, Control) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx_handle_t saved1 = h1.get();

  InterfaceRequest<fidl::test::frobinator::Frobinator> request(std::move(h1));
  EXPECT_TRUE(request.is_valid());
  EXPECT_EQ(saved1, request.channel().get());

  InterfaceRequest<fidl::test::frobinator::Frobinator> request2 =
      std::move(request);
  EXPECT_FALSE(request.is_valid());
  EXPECT_TRUE(request2.is_valid());
  EXPECT_EQ(saved1, request2.channel().get());

  h1 = request2.TakeChannel();
  EXPECT_EQ(saved1, h1.get());
  EXPECT_FALSE(request2.is_valid());

  zx_handle_t saved2 = h2.get();
  request.set_channel(std::move(h2));
  EXPECT_TRUE(request.is_valid());
  EXPECT_EQ(saved2, request.channel().get());

  request = nullptr;
  EXPECT_FALSE(request.is_valid());

  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h1.write(0, "a", 1, nullptr, 0));
}

}  // namespace
}  // namespace fidl
