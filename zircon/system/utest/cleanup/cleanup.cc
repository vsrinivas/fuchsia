// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kMessage[] = "This is a test message, please discard.";

TEST(CleanupTest, CloseOneEndWaitOnOther) {
  // Create a channel, close one end, try to wait on the other. See that
  // the wait succeeds and results in "peer closed".

  zx::channel a, b;
  ASSERT_OK(zx::channel::create(0, &a, &b));
  b.reset();

  zx_signals_t pending;
  ASSERT_OK(
      a.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &pending));
  ASSERT_EQ(pending, ZX_CHANNEL_PEER_CLOSED);
}

TEST(CleanupTest, CloseOneEndWriteFails) {
  // Create a channel, close one end. Then create an event and write a
  // message on the channel sending the event along. See that the write
  // fails (because the other end is closed) and that the event is
  // consumed (because the write failed).

  zx::channel a, b;
  ASSERT_OK(zx::channel::create(0, &a, &b));
  b.reset();

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));
  zx_handle_t handle = event.get();
  ASSERT_STATUS(ZX_ERR_PEER_CLOSED, a.write(0, kMessage, sizeof(kMessage), &handle, 1));
  ASSERT_STATUS(ZX_ERR_BAD_HANDLE, event.signal(0, ZX_EVENT_SIGNALED));
}

TEST(CleanupTest, MessageHandlesAreClosed) {
  // Simulates the case where we prepare a message channel with a
  // message+channelhandle already in it and the far end closed,
  // like we pass to newly created processes, but then (say
  // process creation fails), we delete the other end of the
  // channel we were going to send.  At this point we expect
  // that the channel handle bundled with the message should
  // be closed and waiting on the opposing handle should
  // signal PEER_CLOSED.

  zx::channel a, b;
  zx::channel c, d;
  ASSERT_OK(zx::channel::create(0, &a, &b));
  ASSERT_OK(zx::channel::create(0, &c, &d));

  zx_handle_t handle = d.get();
  ASSERT_OK(a.write(0, kMessage, sizeof(kMessage), &handle, 1));

  a.reset();
  b.reset();

  ASSERT_OK(c.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
}

}  // anonymous namespace
