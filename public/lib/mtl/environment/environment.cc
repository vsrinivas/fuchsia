// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/environment/environment.h"

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/environment/default_async_waiter.h"

namespace mojo {
namespace {

const MojoAsyncWaiter* g_default_async_waiter =
    &mtl::internal::kDefaultAsyncWaiter;

}  // namespace

// static
const MojoAsyncWaiter* Environment::GetDefaultAsyncWaiter() {
  return g_default_async_waiter;
}

// static
void Environment::SetDefaultAsyncWaiter(const MojoAsyncWaiter* async_waiter) {
  g_default_async_waiter =
      async_waiter ? async_waiter : &mtl::internal::kDefaultAsyncWaiter;
}

// static
void Environment::InstantiateDefaultRunLoop() {
  FTL_CHECK(!mtl::MessageLoop::GetCurrent());
  // Not leaked: accessible from |MessageLoop::GetCurrent()|.
  mtl::MessageLoop* message_loop = new mtl::MessageLoop();
  FTL_CHECK(message_loop == mtl::MessageLoop::GetCurrent());
}

// static
void Environment::DestroyDefaultRunLoop() {
  FTL_CHECK(mtl::MessageLoop::GetCurrent());
  delete mtl::MessageLoop::GetCurrent();
  FTL_CHECK(!mtl::MessageLoop::GetCurrent());
}

}  // namespace mojo
