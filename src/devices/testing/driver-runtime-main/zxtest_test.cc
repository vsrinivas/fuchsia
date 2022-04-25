// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
#include <lib/sync/cpp/completion.h>

#include <zxtest/zxtest.h>

namespace {

TEST(ZxTest, GetCurrentDispatcherWorks) { EXPECT_NOT_NULL(fdf::Dispatcher::GetCurrent()->get()); }

TEST(ZxTest, CreateDispatcherWorks) {
  libsync::Completion completion;
  auto dispatcher = fdf::Dispatcher::Create(0, [&](fdf_dispatcher_t*) { completion.Signal(); });
  EXPECT_OK(dispatcher.status_value());
  dispatcher->ShutdownAsync();
  completion.Wait();
}

TEST(ZxTest, DoWork) {
  libsync::Completion completion;
  auto dispatcher = fdf::Dispatcher::Create(0, [&](fdf_dispatcher_t*) { completion.Signal(); });
  ASSERT_OK(dispatcher.status_value());

  async::PostTask(dispatcher->async_dispatcher(), [&]() { completion.Signal(); });
  completion.Wait();
  completion.Reset();

  dispatcher->ShutdownAsync();
  completion.Wait();
}

}  // namespace
