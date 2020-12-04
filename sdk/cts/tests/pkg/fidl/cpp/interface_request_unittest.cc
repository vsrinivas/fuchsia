// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/interface_request.h"

#include <lib/zx/channel.h>

#include <utility>

#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(InterfaceRequest, Trivial) { InterfaceRequest<fidl::test::frobinator::Frobinator> request; }

TEST(InterfaceRequest, Control) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  zx_handle_t saved1 = h1.get();

  InterfaceRequest<fidl::test::frobinator::Frobinator> request(std::move(h1));
  EXPECT_TRUE(request.is_valid());
  EXPECT_EQ(saved1, request.channel().get());

  InterfaceRequest<fidl::test::frobinator::Frobinator> request2 = std::move(request);
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

TEST(InterfaceRequest, Close) {
  fidl::test::AsyncLoopForTest loop;

  fidl::test::frobinator::FrobinatorPtr ptr;
  zx_status_t error = 0;
  ptr.set_error_handler([&error](zx_status_t remote_error) { error = remote_error; });

  InterfaceRequest<fidl::test::frobinator::Frobinator> request = ptr.NewRequest();
  EXPECT_TRUE(request.is_valid());

  constexpr zx_status_t kSysError = 0xabDECADE;

  EXPECT_EQ(ZX_OK, request.Close(kSysError));
  EXPECT_FALSE(request.is_valid());

  // Should only be able to call Close successfully once
  EXPECT_EQ(ZX_ERR_BAD_STATE, request.Close(ZX_ERR_BAD_STATE));

  loop.RunUntilIdle();
  EXPECT_EQ(kSysError, error);
}

}  // namespace
}  // namespace fidl
