// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

static const char* msg = "This is a test message, please discard.";

bool cleanup_test(void) {
  BEGIN_TEST;
  zx_handle_t p0[2], p1[2];
  zx_signals_t pending;
  zx_status_t r;

  // TEST1
  // Create a channel, close one end, try to wait on the other.
  r = zx_channel_create(0, p1, p1 + 1);
  ASSERT_EQ(r, ZX_OK, "cleanup-test: channel create 1 failed");

  zx_handle_close(p1[1]);
  unittest_printf("cleanup-test: about to wait, should return with PEER_CLOSED\n");
  r = zx_object_wait_one(p1[0], ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE,
                         &pending);
  ASSERT_EQ(r, ZX_OK, "cleanup-test: FAILED");

  ASSERT_EQ(pending, ZX_CHANNEL_PEER_CLOSED, "cleanup-test: FAILED");
  unittest_printf("cleanup-test: SUCCESS, observed PEER_CLOSED signal\n\n");
  zx_handle_close(p1[0]);

  // TEST2
  // Create a channel, close one end. Then create an event and write a
  // message on the channel sending the event along. See that the write
  // fails (because the other end is closed) and that the event is
  // consumed (because the write failed).
  r = zx_channel_create(0, p1, p1 + 1);
  ASSERT_EQ(r, ZX_OK, "cleanup-test: channel create 1 failed");
  zx_handle_close(p1[1]);

  zx_handle_t event = ZX_HANDLE_INVALID;
  r = zx_event_create(0u, &event);
  ASSERT_EQ(r, ZX_OK, "");
  ASSERT_NE(event, ZX_HANDLE_INVALID, "cleanup-test: event create failed");
  r = zx_channel_write(p1[0], 0, &msg, sizeof(msg), &event, 1);
  ASSERT_EQ(r, ZX_ERR_PEER_CLOSED, "cleanup-test: unexpected message_write return code");

  r = zx_object_signal(event, 0u, ZX_EVENT_SIGNALED);
  ASSERT_EQ(r, ZX_ERR_BAD_HANDLE, "cleanup-test: able to signal event!");
  unittest_printf("cleanup-test: SUCCESS, event is closed\n\n");

  zx_handle_close(p1[0]);

  // TEST3
  // Simulates the case where we prepare a message channel with a
  // message+channelhandle already in it and the far end closed,
  // like we pass to newly created processes, but then (say
  // process creation fails), we delete the other end of the
  // channel we were going to send.  At this point we expect
  // that the channel handle bundled with the message should
  // be closed and waiting on the opposing handle should
  // signal PEER_CLOSED.
  r = zx_channel_create(0, p0, p0 + 1);
  ASSERT_EQ(r, ZX_OK, "cleanup-test: channel create 0 failed");

  r = zx_channel_create(0, p1, p1 + 1);
  ASSERT_EQ(r, ZX_OK, "cleanup-test: channel create 1 failed");

  r = zx_channel_write(p0[0], 0, &msg, sizeof(msg), &p1[1], 1);
  ASSERT_EQ(r, ZX_OK, "cleanup-test: channel write failed");

  zx_handle_close(p0[0]);
  zx_handle_close(p0[1]);

  unittest_printf("cleanup-test: about to wait, should return with PEER_CLOSED\n");
  r = zx_object_wait_one(p1[0], ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, NULL);
  ASSERT_EQ(r, ZX_OK, "cleanup-test: FAILED");

  unittest_printf("cleanup-test: PASSED\n");
  zx_handle_close(p1[0]);
  END_TEST;
}

BEGIN_TEST_CASE(cleanup_tests)
RUN_TEST(cleanup_test)
END_TEST_CASE(cleanup_tests)
