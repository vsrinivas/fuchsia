// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/interface_ptr.h"

#include <lib/fidl/cpp/message_buffer.h>

#include <thread>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(SynchronousInterfacePtr, Trivial) { fidl::test::frobinator::FrobinatorSync2Ptr ptr; }

TEST(SynchronousInterfacePtr, Control) {
  fidl::test::frobinator::FrobinatorSync2Ptr ptr;
  auto request = ptr.NewRequest();

  std::thread client([ptr = std::move(ptr)]() mutable {
    ptr->Frob("one");

    fidl::StringPtr result;
    ptr->Grob("two", &result);

    EXPECT_FALSE(result.is_null());
    EXPECT_EQ("response", *result);
  });

  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  EXPECT_EQ(ZX_OK, binding.Bind(std::move(request)));

  binding.WaitForMessage();
  binding.WaitForMessage();

  client.join();
}

TEST(SynchronousInterfacePtr, Unbind) {
  fidl::test::frobinator::FrobinatorSync2Ptr ptr;
  auto request = ptr.NewRequest();

  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  EXPECT_EQ(ZX_OK, binding.Bind(std::move(request)));

  auto handle = ptr.Unbind();
  EXPECT_TRUE(handle.is_valid());
}

}  // namespace
}  // namespace fidl
