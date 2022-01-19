// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/zx/channel.h>

#include <sanitizer/lsan_interface.h>
#include <zxtest/zxtest.h>

#include "lsan_disabler.h"
#include "mock_client_impl.h"

using ::fidl_testing::TestProtocol;

namespace fidl {
namespace internal {

TEST(ClientController, BindingTwicePanics) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ClientController controller;
  fidl::WireAsyncEventHandler<TestProtocol>* event_handler = nullptr;

  controller.Bind(MakeAnyTransport(std::move(h1)), loop.dispatcher(),
                  MakeAnyEventDispatcher(event_handler), fidl::AnyTeardownObserver::Noop(),
                  ThreadingPolicy::kCreateAndTeardownFromAnyThread);

  ASSERT_DEATH([&] {
    fidl_testing::RunWithLsanDisabled([&] {
      controller.Bind(MakeAnyTransport(std::move(h2)), loop.dispatcher(),
                      MakeAnyEventDispatcher(event_handler), fidl::AnyTeardownObserver::Noop(),
                      ThreadingPolicy::kCreateAndTeardownFromAnyThread);
    });
  });
}

}  // namespace internal
}  // namespace fidl
