// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/zx/channel.h>

#include <sanitizer/lsan_interface.h>
#include <zxtest/zxtest.h>

#include "mock_client_impl.h"

using ::fidl_testing::TestProtocol;

namespace fidl {
namespace internal {

TEST(ClientController, BindingTwicePanics) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ClientController controller;

  controller.Bind(std::make_shared<WireClientImpl<TestProtocol>>(), std::move(h1),
                  loop.dispatcher(), nullptr, fidl::internal::AnyTeardownObserver::Noop(),
                  fidl::internal::ThreadingPolicy::kCreateAndTeardownFromAnyThread);

  ASSERT_DEATH([&] {
#if __has_feature(address_sanitizer) || __has_feature(leak_sanitizer)
    // Disable LSAN for this thread. It is expected to leak by way of a crash.
    __lsan::ScopedDisabler _;
#endif
    controller.Bind(std::make_shared<WireClientImpl<TestProtocol>>(), std::move(h2),
                    loop.dispatcher(), nullptr, fidl::internal::AnyTeardownObserver::Noop(),
                    fidl::internal::ThreadingPolicy::kCreateAndTeardownFromAnyThread);
  });
}

}  // namespace internal
}  // namespace fidl
