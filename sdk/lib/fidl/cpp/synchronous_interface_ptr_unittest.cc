// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/message_buffer.h>

#include <thread>

#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(SynchronousInterfacePtr, Trivial) { fidl::test::frobinator::FrobinatorSyncPtr ptr; }

TEST(SynchronousInterfacePtr, Control) {
  fidl::test::frobinator::FrobinatorSyncPtr ptr;
  auto request = ptr.NewRequest();

  std::thread client([ptr = std::move(ptr)]() mutable {
    ptr->Frob("one");

    std::string result;
    ptr->Grob("two", &result);

    EXPECT_EQ("response", result);
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
  fidl::test::frobinator::FrobinatorSyncPtr ptr;
  auto request = ptr.NewRequest();

  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  EXPECT_EQ(ZX_OK, binding.Bind(std::move(request)));

  auto handle = ptr.Unbind();
  EXPECT_TRUE(handle.is_valid());
}

TEST(SynchronousInterfacePtr, ChannelGetter) {
  fidl::test::frobinator::FrobinatorSyncPtr ptr;
  EXPECT_FALSE(*ptr.unowned_channel());
  auto request = ptr.NewRequest();
  EXPECT_TRUE(*ptr.unowned_channel());
  ptr.Unbind();
  EXPECT_FALSE(*ptr.unowned_channel());
}

}  // namespace
}  // namespace fidl
