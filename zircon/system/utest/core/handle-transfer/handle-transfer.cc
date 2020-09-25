// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/thread.h>
#include <stdio.h>
#include <threads.h>
#include <time.h>
#include <zircon/threads.h>

#include <iterator>

#include <zxtest/zxtest.h>

namespace {

// This example tests transferring channel handles through channels. To do so, it:
//   Creates two channels, a and b.
//   Sends message 0 into a_client
//   Sends a_remote into b_client
//   Sends message 1 into a_client
//   Reads a_remote from b_remote (should receive
//   a_remote, possibly with a new value)
//   Sends 2 into a_client
//   Reads from a_remote. Should read 0, 1, 2 in that order.
TEST(HandleTransferTest, OverChannelThenRead) {
  zx::channel a_client, a_remote;
  zx::channel b_client, b_remote;

  ASSERT_OK(zx::channel::create(0, &a_client, &a_remote));
  ASSERT_OK(zx::channel::create(0, &b_client, &b_remote));

  constexpr char kMessage[] = {0, 1, 2};
  ASSERT_OK(a_client.write(0u, &kMessage[0], 1u, nullptr, 0u));
  zx_handle_t a_remote_raw = a_remote.release();
  ASSERT_OK(b_client.write(0u, nullptr, 0u, &a_remote_raw, 1u));

  ASSERT_OK(a_client.write(0u, &kMessage[1], 1u, nullptr, 0u));

  a_remote_raw = ZX_HANDLE_INVALID;
  uint32_t num_bytes = 0u;
  uint32_t num_handles = 1u;
  ASSERT_OK(
      b_remote.read(0u, nullptr, &a_remote_raw, num_bytes, num_handles, &num_bytes, &num_handles));
  ASSERT_EQ(num_handles, 1);
  a_remote.reset(a_remote_raw);
  ASSERT_TRUE(a_remote.is_valid());

  ASSERT_OK(a_client.write(0u, &kMessage[2], 1u, nullptr, 0u));

  for (size_t i = 0; i < std::size(kMessage); ++i) {
    char incoming_byte;
    num_bytes = 1u;
    num_handles = 0u;
    ASSERT_OK(
        a_remote.read(0u, &incoming_byte, nullptr, num_bytes, num_handles, nullptr, &num_handles));
    ASSERT_EQ(num_handles, 0);
    ASSERT_EQ(kMessage[i], incoming_byte);
  }
}

constexpr zx::duration kPollingInterval = zx::msec(1);

// Wait, possibly forever, until |thread| has entered |state|.
zx_status_t WaitForState(const zx::unowned_thread& thread, zx_thread_state_t state) {
  while (true) {
    zx_info_thread_t info;
    zx_status_t status = thread->get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      return status;
    }
    if (info.state == state) {
      return ZX_OK;
    }
    zx::nanosleep(zx::deadline_after(kPollingInterval));
  }
}

// This tests canceling a wait when a handle is transferred.
//
// There are two channels, a and b. One thread waits on a[0]. The other thread sends a[0] through
// channel b and sees that once it has been read out of b, the wait is canceled.
//
// See [fxbug.dev/30064].
TEST(HandleTransferTest, CancelsWait) {
  auto wait_on_channel = [](void* arg) -> int {
    zx::unowned_channel channel(*reinterpret_cast<zx_handle_t*>(arg));
    zx_signals_t signals = {};
    return channel->wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signals);
  };

  zx::channel a[2];
  zx::channel b[2];
  ASSERT_OK(zx::channel::create(0, &a[0], &a[1]));
  ASSERT_OK(zx::channel::create(0, &b[0], &b[1]));

  // Start the thread.
  thrd_t waiter_thread;
  zx_handle_t handle = a[0].get();
  ASSERT_EQ(thrd_success, thrd_create(&waiter_thread, wait_on_channel, &handle));

  // Wait for it to enter zx_object_wait_one.
  zx::unowned_thread thread(thrd_get_zx_handle(waiter_thread));
  ASSERT_OK(WaitForState(thread, ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

  // Send a[0] through b.
  handle = a[0].release();
  ASSERT_OK(b[0].write(0, nullptr, 0, &handle, 1));
  uint32_t num_handles = 0;

  // See that it's still blocked.
  zx_info_thread_t info;
  ASSERT_OK(thread->get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(ZX_THREAD_STATE_BLOCKED_WAIT_ONE, info.state);

  // Pulling the handle out of b cancels the wait.
  ASSERT_OK(b[1].read(0, nullptr, a[0].reset_and_get_address(), 0, 1, nullptr, &num_handles));
  ASSERT_EQ(1, num_handles);

  // Join the thread and see that it was canceled.
  int result = ZX_ERR_INTERNAL;
  ASSERT_EQ(thrd_success, thrd_join(waiter_thread, &result));
  ASSERT_EQ(ZX_ERR_CANCELED, result);
}

}  // namespace
