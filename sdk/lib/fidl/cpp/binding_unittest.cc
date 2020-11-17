// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/binding.h"

#include <string>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

TEST(Binding, Trivial) { Binding<fidl::test::frobinator::Frobinator> binding(nullptr); }

TEST(Binding, Control) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(nullptr, binding.dispatcher());
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));
  EXPECT_EQ(loop.dispatcher(), binding.dispatcher());

  ptr->Frob("hello");

  EXPECT_TRUE(impl.frobs.empty());
  EXPECT_TRUE(impl.grobs.empty());

  EXPECT_EQ(ZX_OK, binding.WaitForMessage());

  EXPECT_EQ(1u, impl.frobs.size());
  EXPECT_EQ("hello", impl.frobs[0]);
  EXPECT_TRUE(impl.grobs.empty());

  impl.frobs.clear();

  std::vector<std::string> responses;
  ptr->Grob("world", [&responses](StringPtr value) {
    EXPECT_TRUE(value.has_value());
    responses.push_back(std::move(*value));
  });

  EXPECT_TRUE(impl.frobs.empty());
  EXPECT_TRUE(impl.grobs.empty());
  EXPECT_TRUE(responses.empty());

  loop.RunUntilIdle();

  EXPECT_TRUE(impl.frobs.empty());
  EXPECT_EQ(1u, impl.grobs.size());
  EXPECT_EQ("world", impl.grobs[0]);
  EXPECT_EQ(1u, responses.size());
  EXPECT_EQ("response", responses[0]);
}

TEST(Binding, Bind) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  EXPECT_FALSE(binding.is_bound());
  EXPECT_EQ(&impl, binding.impl());

  auto handle = binding.NewBinding();
  EXPECT_TRUE(handle.is_valid());
  EXPECT_TRUE(binding.is_bound());
  EXPECT_EQ(&impl, binding.impl());

  auto request = binding.Unbind();
  EXPECT_TRUE(request.is_valid());
  EXPECT_FALSE(binding.is_bound());
  EXPECT_EQ(&impl, binding.impl());

  EXPECT_EQ(ZX_OK, binding.Bind(std::move(request)));
  EXPECT_FALSE(request.is_valid());
  EXPECT_TRUE(binding.is_bound());
  EXPECT_EQ(&impl, binding.impl());
}

TEST(Binding, ConstructBound) {
  fidl::test::AsyncLoopForTest loop;

  InterfaceHandle<fidl::test::frobinator::Frobinator> handle;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl, handle.NewRequest());
  EXPECT_TRUE(handle.is_valid());
  EXPECT_TRUE(binding.is_bound());
  EXPECT_EQ(&impl, binding.impl());
}

TEST(Binding, ErrorHandler) {
  fidl::test::AsyncLoopForTest loop;

  InterfaceHandle<fidl::test::frobinator::Frobinator> handle;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl, handle.NewRequest());
  EXPECT_TRUE(handle.is_valid());
  EXPECT_TRUE(binding.is_bound());
  EXPECT_FALSE(binding.has_error_handler());
  EXPECT_EQ(&impl, binding.impl());

  int error_count = 0;
  binding.set_error_handler([&error_count, &binding](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
    EXPECT_FALSE(binding.is_bound());
  });

  EXPECT_TRUE(binding.has_error_handler());
  EXPECT_EQ(ZX_OK, handle.channel().write(0, "a", 1, nullptr, 0));
  EXPECT_EQ(0, error_count);

  loop.RunUntilIdle();

  EXPECT_EQ(1, error_count);
}

TEST(Binding, DestructDuringErrorHandler) {
  fidl::test::AsyncLoopForTest loop;

  InterfaceHandle<fidl::test::frobinator::Frobinator> handle;

  test::FrobinatorImpl impl;
  auto binding = std::make_unique<Binding<fidl::test::frobinator::Frobinator>>(&impl);
  binding->Bind(handle.NewRequest());
  EXPECT_TRUE(handle.is_valid());
  EXPECT_TRUE(binding->is_bound());
  EXPECT_FALSE(binding->has_error_handler());
  EXPECT_EQ(&impl, binding->impl());

  int error_count = 0;
  binding->set_error_handler([&error_count, &binding](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
    EXPECT_FALSE(binding->is_bound());
    binding.reset();
  });

  EXPECT_TRUE(binding->has_error_handler());
  EXPECT_EQ(ZX_OK, handle.channel().write(0, "a", 1, nullptr, 0));
  EXPECT_EQ(0, error_count);

  loop.RunUntilIdle();

  EXPECT_EQ(1, error_count);
}

TEST(Binding, PeerClosedTriggersErrorHandler) {
  fidl::test::AsyncLoopForTest loop;
  InterfaceHandle<fidl::test::frobinator::Frobinator> handle;
  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl, handle.NewRequest());

  int error_count = 0;
  binding.set_error_handler([&error_count, &binding](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, status);
    ++error_count;
    EXPECT_FALSE(binding.is_bound());
  });

  EXPECT_TRUE(binding.has_error_handler());
  handle = nullptr;
  EXPECT_EQ(0, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, error_count);
}

TEST(Binding, UnbindDoesNotTriggerErrorHandler) {
  fidl::test::AsyncLoopForTest loop;
  InterfaceHandle<fidl::test::frobinator::Frobinator> handle;
  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl, handle.NewRequest());

  int error_count = 0;
  binding.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
  });

  EXPECT_TRUE(binding.has_error_handler());
  binding.Unbind();
  EXPECT_EQ(0, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, error_count);
}

TEST(Binding, EpitaphReceivedWhenBindingClosed) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  constexpr zx_status_t kSysError = 0xabDECADE;

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  zx_status_t error = 0;
  ptr.set_error_handler([&error](zx_status_t remote_error) { error = remote_error; });

  EXPECT_EQ(ZX_OK, binding.Close(kSysError));

  // Check that you can only call Close once...
  EXPECT_EQ(ZX_ERR_BAD_STATE, binding.Close(kSysError));

  loop.RunUntilIdle();

  EXPECT_EQ(kSysError, error);

  ptr->Frob("This should break");
  EXPECT_EQ(ZX_ERR_BAD_STATE, binding.WaitForMessage());
}

TEST(Binding, ErrorHandlerCalledWhenInterfacePtrClosed) {
  fidl::test::AsyncLoopForTest loop;

  test::FrobinatorImpl impl;
  Binding<fidl::test::frobinator::Frobinator> binding(&impl);

  fidl::test::frobinator::FrobinatorPtr ptr;
  EXPECT_EQ(ZX_OK, binding.Bind(ptr.NewRequest()));

  zx_status_t error = 0;
  binding.set_error_handler([&error](zx_status_t remote_error) { error = remote_error; });
  EXPECT_TRUE(binding.has_error_handler());

  ptr.Unbind();

  loop.RunUntilIdle();

  EXPECT_EQ(ZX_ERR_PEER_CLOSED, error);

  ptr->Frob("This should break");
  EXPECT_EQ(ZX_ERR_BAD_STATE, binding.WaitForMessage());
}

}  // namespace
}  // namespace fidl
