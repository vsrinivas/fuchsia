// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_CLIENT_CHECKERS_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_CLIENT_CHECKERS_H_

#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/client_base.h>

#include <zxtest/zxtest.h>

// The classes in this header are made friends of relevant binding runtime
// types, such that they may check/assert on the binding internal state.
namespace fidl_testing {

class ClientBaseChecker {
 public:
  static std::shared_ptr<fidl::internal::AnyTransport> GetTransport(
      fidl::internal::ClientBase* client_base) {
    return client_base->GetTransport();
  }
};

class ClientChecker {
 public:
  // Asserts that the contained client implementation object is not null.
  template <typename ClientLike>
  static void AssertImplNotNull(const ClientLike& client) {
    ASSERT_NOT_NULL(client.controller_.client_impl_.get());
  }

  // Asserts that the contained client implementation object is null.
  template <typename ClientLike>
  static void AssertImplNull(const ClientLike& client) {
    ASSERT_NULL(client.controller_.client_impl_.get());
  }

  // Gets a pointer to the internal state.
  template <typename ClientLike>
  static fidl::internal::ClientBase* GetClientBase(const ClientLike& client) {
    return client.controller_.client_impl_.get();
  }

  template <typename ClientLike>
  static std::shared_ptr<fidl::internal::AnyTransport> GetTransport(const ClientLike& client) {
    return ClientBaseChecker::GetTransport(GetClientBase(client));
  }
};

}  // namespace fidl_testing

#define ASSERT_CLIENT_IMPL_NOT_NULL(client) \
  ASSERT_NO_FAILURES(::fidl_testing::ClientChecker::AssertImplNotNull(client))
#define ASSERT_CLIENT_IMPL_NULL(client) \
  ASSERT_NO_FAILURES(::fidl_testing::ClientChecker::AssertImplNull(client))

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_CLIENT_CHECKERS_H_
