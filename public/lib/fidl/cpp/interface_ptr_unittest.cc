// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/interface_ptr.h"

#include <lib/fidl/cpp/message_buffer.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"
#include "lib/fxl/functional/make_copyable.h"

namespace fidl {
namespace {

TEST(InterfacePtr, Trivial) { fidl::test::frobinator::FrobinatorPtr ptr; }

TEST(InterfacePtr, Control) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  ptr->Frob("one");
  EXPECT_TRUE(impl.frobs.empty());

  loop.RunUntilIdle();

  EXPECT_EQ(1u, impl.frobs.size());

  EXPECT_TRUE(ptr.is_bound());
  auto handle = ptr.Unbind();
  EXPECT_TRUE(handle);
  EXPECT_FALSE(ptr.is_bound());
  EXPECT_EQ(ZX_OK, ptr.Bind(std::move(handle)));
  EXPECT_TRUE(ptr.is_bound());

  EXPECT_EQ(ZX_ERR_TIMED_OUT, ptr.WaitForResponseUntil(zx::time()));
}

TEST(InterfacePtr, Events) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  std::vector<std::string> hrobs;
  ptr.events().Hrob = [&hrobs](StringPtr value) { hrobs.push_back(value); };

  binding.events().Hrob("one");
  EXPECT_TRUE(hrobs.empty());

  loop.RunUntilIdle();

  EXPECT_EQ(1u, hrobs.size());
}

TEST(InterfacePtr, EventWithoutListener) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  binding.events().Hrob("one");

  loop.RunUntilIdle();
}

TEST(InterfacePtr, MoveConstructWithOutstandingTransaction) {
  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::frobinator::FrobinatorPtr ptr;

  int error_count = 0;
  ptr.set_error_handler([&error_count]() { ++error_count; });

  EXPECT_EQ(ZX_OK, ptr.Bind(std::move(h1)));

  int reply_count = 0;
  ptr->Grob("one", [&reply_count](StringPtr value) {
    ++reply_count;
    EXPECT_FALSE(value.is_null());
    EXPECT_EQ("one", *value);
  });

  EXPECT_EQ(0, reply_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, reply_count);

  fidl::test::frobinator::FrobinatorPtr ptr2(std::move(ptr));
  EXPECT_FALSE(ptr.is_bound());
  EXPECT_TRUE(ptr2.is_bound());

  MessageBuffer buffer;
  Message message = buffer.CreateEmptyMessage();
  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
  EXPECT_EQ(ZX_OK, message.Write(h2.get(), 0));

  EXPECT_EQ(0, reply_count);
  EXPECT_EQ(ZX_ERR_BAD_STATE, ptr.WaitForResponse());
  EXPECT_EQ(0, reply_count);
  EXPECT_EQ(ZX_OK, ptr2.WaitForResponse());
  EXPECT_EQ(1, reply_count);

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_OK, h2.write(0, "a", 1, nullptr, 0));
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(ptr2.is_bound());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ptr2.WaitForResponse());
  EXPECT_EQ(1, reply_count);
  EXPECT_EQ(1, error_count);
  EXPECT_FALSE(ptr2.is_bound());
}

TEST(InterfacePtr, MoveAssignWithOutstandingTransaction) {
  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::frobinator::FrobinatorPtr ptr;

  int error_count = 0;
  ptr.set_error_handler([&error_count]() { ++error_count; });

  EXPECT_EQ(ZX_OK, ptr.Bind(std::move(h1)));

  int reply_count = 0;
  ptr->Grob("one", [&reply_count](StringPtr value) {
    ++reply_count;
    EXPECT_FALSE(value.is_null());
    EXPECT_EQ("one", *value);
  });

  EXPECT_EQ(0, reply_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, reply_count);

  fidl::test::frobinator::FrobinatorPtr ptr2 = std::move(ptr);
  EXPECT_FALSE(ptr.is_bound());
  EXPECT_TRUE(ptr2.is_bound());

  MessageBuffer buffer;
  Message message = buffer.CreateEmptyMessage();
  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
  EXPECT_EQ(ZX_OK, message.Write(h2.get(), 0));

  EXPECT_EQ(0, reply_count);
  EXPECT_EQ(ZX_ERR_BAD_STATE, ptr.WaitForResponse());
  EXPECT_EQ(0, reply_count);
  EXPECT_EQ(ZX_OK, ptr2.WaitForResponse());
  EXPECT_EQ(1, reply_count);

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_OK, h2.write(0, "a", 1, nullptr, 0));
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(ptr2.is_bound());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, ptr2.WaitForResponse());
  EXPECT_EQ(1, reply_count);
  EXPECT_EQ(1, error_count);
  EXPECT_FALSE(ptr2.is_bound());
}

TEST(InterfacePtr, MoveConstructWithEvents) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  std::vector<std::string> hrobs;
  ptr.events().Hrob = [&hrobs](StringPtr value) { hrobs.push_back(value); };

  binding.events().Hrob("one");
  EXPECT_TRUE(hrobs.empty());

  // Move |ptr| and show that we still get our events.
  fidl::test::frobinator::FrobinatorPtr ptr2(std::move(ptr));
  ASSERT_FALSE(ptr);

  loop.RunUntilIdle();

  EXPECT_EQ(1u, hrobs.size());
}

TEST(InterfacePtr, MoveAssignWithEvents) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  std::vector<std::string> hrobs;
  ptr.events().Hrob = [&hrobs](StringPtr value) { hrobs.push_back(value); };

  binding.events().Hrob("one");
  EXPECT_TRUE(hrobs.empty());

  fidl::test::frobinator::FrobinatorPtr ptr2;
  ptr2 = std::move(ptr);
  ASSERT_FALSE(ptr);

  loop.RunUntilIdle();

  EXPECT_EQ(1u, hrobs.size());
}

TEST(InterfacePtr, MoveIntoMethodCapture) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  std::vector<std::string> grobs;
  ptr->Grob("one", fxl::MakeCopyable([moved = std::move(ptr), &grobs](
                                         StringPtr s) { grobs.push_back(s); }));
  EXPECT_FALSE(ptr.is_bound());
  EXPECT_TRUE(grobs.empty());

  loop.RunUntilIdle();

  EXPECT_EQ(1u, grobs.size());
}

}  // namespace
}  // namespace fidl
