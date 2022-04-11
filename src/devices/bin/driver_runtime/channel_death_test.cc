// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/bin/driver_runtime/test_utils.h"

TEST(ChannelDeathTest, CloseCrashesIfPendingWaitNotCancelled) {
  test_utils::RunWithLsanDisabled([&] {
    uint32_t fake_driver;
    driver_context::PushDriver(&fake_driver);
    auto pop_driver = fit::defer([]() { driver_context::PopDriver(); });

    auto dispatcher = fdf::Dispatcher::Create(
        0, [](fdf_dispatcher_t* dispatcher) {}, "");
    ASSERT_OK(dispatcher.status_value());

    auto channels = fdf::ChannelPair::Create(0);
    ASSERT_OK(channels.status_value());
    auto local = std::move(channels->end0);
    auto remote = std::move(channels->end1);

    auto channel_read = std::make_unique<fdf::ChannelRead>(
        remote.get(), 0,
        [](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
          ASSERT_FALSE(true);  // This should never be called.
        });
    ASSERT_OK(channel_read->Begin(dispatcher->get()));

    ASSERT_DEATH([&] {
      // Close the channel, this should crash.
      remote.reset();
    });

    // Since |Channel::Close| crashed, the channel will be left in a state which
    // cannot be properly destructed.
    dispatcher->release();
    local.release();
    remote.release();
    channel_read.release();
  });
}
