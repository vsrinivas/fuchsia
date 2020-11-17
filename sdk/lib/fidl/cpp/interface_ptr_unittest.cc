// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/interface_ptr.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/message_buffer.h>

#include <zxtest/zxtest.h>

#include "fidl/test/frobinator/cpp/fidl.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(InterfacePtr, Trivial) { fidl::test::frobinator::FrobinatorPtr ptr; }

TEST(InterfacePtr, Control) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(nullptr, ptr.dispatcher());
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));
  EXPECT_EQ(loop.dispatcher(), ptr.dispatcher());

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
}

TEST(InterfacePtr, BindToSpecificDispatcher) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest(loop.dispatcher()), loop.dispatcher()));
  EXPECT_TRUE(ptr.is_bound());

  ptr->Frob("one");
  EXPECT_TRUE(impl.frobs.empty());

  loop.RunUntilIdle();

  EXPECT_EQ(1u, impl.frobs.size());
}

TEST(InterfacePtr, Events) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  std::vector<std::string> hrobs;
  ptr.events().Hrob = [&hrobs](StringPtr value) {
    EXPECT_TRUE(value.has_value());
    hrobs.push_back(value.value());
  };

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
  ptr.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
  });

  EXPECT_EQ(ZX_OK, ptr.Bind(std::move(h1)));

  int reply_count = 0;
  ptr->Grob("one", [&reply_count](StringPtr value) {
    ++reply_count;
    EXPECT_TRUE(value.has_value());
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
  loop.RunUntilIdle();
  EXPECT_EQ(1, reply_count);

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_OK, h2.write(0, "a", 1, nullptr, 0));
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(ptr2.is_bound());
  loop.RunUntilIdle();
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
  ptr.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
  });

  EXPECT_EQ(ZX_OK, ptr.Bind(std::move(h1)));

  int reply_count = 0;
  ptr->Grob("one", [&reply_count](StringPtr value) {
    ++reply_count;
    EXPECT_TRUE(value.has_value());
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
  loop.RunUntilIdle();
  EXPECT_EQ(1, reply_count);

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_OK, h2.write(0, "a", 1, nullptr, 0));
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(ptr2.is_bound());
  loop.RunUntilIdle();
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
  ptr.events().Hrob = [&hrobs](StringPtr value) {
    EXPECT_TRUE(value.has_value());
    hrobs.push_back(value.value());
  };

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
  ptr.events().Hrob = [&hrobs](StringPtr value) {
    EXPECT_TRUE(value.has_value());
    hrobs.push_back(value.value());
  };

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
  ptr->Grob("one", [moved = std::move(ptr), &grobs](StringPtr s) {
    EXPECT_TRUE(s.has_value());
    grobs.push_back(s.value());
  });
  EXPECT_FALSE(ptr.is_bound());
  EXPECT_TRUE(grobs.empty());

  loop.RunUntilIdle();

  EXPECT_EQ(1u, grobs.size());
}

TEST(InterfacePtr, ErrorNoValues) {
  test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  bool replied = false;
  ptr->Fail(true, [&replied](fit::result<void, uint32_t> result) {
    ASSERT_FALSE(replied);
    replied = true;
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.error(), 42U);
  });
  EXPECT_FALSE(replied);
  loop.RunUntilIdle();
  EXPECT_TRUE(replied);

  replied = false;
  ptr->Fail(false, [&replied](fit::result<void, uint32_t> result) {
    ASSERT_FALSE(replied);
    replied = true;
    EXPECT_TRUE(result.is_ok());
  });
  EXPECT_FALSE(replied);
  loop.RunUntilIdle();
  EXPECT_TRUE(replied);
}

TEST(InterfacePtr, ErrorOneValue) {
  test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  bool replied = false;
  ptr->FailHard(true, [&replied](fit::result<std::string, uint32_t> result) {
    ASSERT_FALSE(replied);
    replied = true;
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.error(), 42U);
  });
  EXPECT_FALSE(replied);
  loop.RunUntilIdle();
  EXPECT_TRUE(replied);

  replied = false;
  ptr->FailHard(false, [&replied](fit::result<std::string, uint32_t> result) {
    ASSERT_FALSE(replied);
    replied = true;
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), "hello, world");
  });
  EXPECT_FALSE(replied);
  loop.RunUntilIdle();
  EXPECT_TRUE(replied);
}

TEST(InterfacePtr, ErrorTwoValues) {
  test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  bool replied = false;
  ptr->FailHardest(true,
                   [&replied](fit::result<std::tuple<std::string, std::string>, uint32_t> result) {
                     ASSERT_FALSE(replied);
                     replied = true;
                     EXPECT_TRUE(result.is_error());
                     EXPECT_EQ(result.error(), 42U);
                   });
  EXPECT_FALSE(replied);
  loop.RunUntilIdle();
  EXPECT_TRUE(replied);

  replied = false;
  ptr->FailHardest(false,
                   [&replied](fit::result<std::tuple<std::string, std::string>, uint32_t> result) {
                     ASSERT_FALSE(replied);
                     replied = true;
                     EXPECT_TRUE(result.is_ok());
                     EXPECT_EQ(std::get<0>(result.value()), "hello");
                     EXPECT_EQ(std::get<1>(result.value()), "world");
                   });
  EXPECT_FALSE(replied);
  loop.RunUntilIdle();
  EXPECT_TRUE(replied);
}

}  // namespace
}  // namespace fidl
