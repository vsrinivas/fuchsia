// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

int reply_handle_basic(void) {
    BEGIN_TEST;

    mx_handle_t p1[2], p2[2];
    mx_status_t r;

    r = mx_channel_create(0, p1, p1 + 1);
    ASSERT_EQ(r, NO_ERROR, "failed to create channel p1");

    r = mx_channel_write(p1[0], 0, "hello", 6, &p1[0], 1);
    ASSERT_EQ(r, ERR_NOT_SUPPORTED, "expected failure");

    r = mx_channel_write(p1[1], 0, "hello", 6, &p1[1], 1);
    ASSERT_EQ(r, ERR_NOT_SUPPORTED, "expected failure");

    r = mx_channel_create(MX_CHANNEL_CREATE_REPLY_CHANNEL, p2, p2 + 1);
    ASSERT_EQ(r, NO_ERROR, "failed to create channel p2");

    r = mx_channel_write(p2[1], 0, "hello", 6, NULL, 0);
    ASSERT_EQ(r, ERR_BAD_STATE, "expected failure");

    r = mx_channel_write(p2[1], 0, "hello", 6, &p1[1], 1);
    ASSERT_EQ(r, ERR_BAD_STATE, "expected failure");

    mx_handle_t har1[2] = {p2[1], p1[1]};
    r = mx_channel_write(p2[1], 0, "hello", 6, har1, 2);
    ASSERT_EQ(r, ERR_BAD_STATE, "expected failure");

    mx_handle_t har2[2] = {p1[1], p2[1]};
    r = mx_channel_write(p2[1], 0, "hello", 6, har2, 2);
    ASSERT_EQ(r, NO_ERROR, "expected success");

    END_TEST;
}

int reply_handle_rw(void) {
    BEGIN_TEST;

    mx_handle_t p1[2], p2[2];
    mx_handle_t p;
    mx_status_t r;
    char msg[128];

    r = mx_channel_create(0, p1, p1 + 1);
    snprintf(msg, sizeof(msg), "failed to create channel1 %d\n", r);
    ASSERT_EQ(r, 0, msg);

    r = mx_channel_create(MX_CHANNEL_CREATE_REPLY_CHANNEL, p2, p2 + 1);
    snprintf(msg, sizeof(msg), "failed to create channel2 %d\n", r);
    ASSERT_EQ(r, 0, msg);

    // send a message and p2[1] through p1[0]
    r = mx_channel_write(p1[0], 0, "hello", 6, &p2[1], 1);
    snprintf(msg, sizeof(msg), "failed to write message+handle to p1[0] %d\n", r);
    EXPECT_GE(r, 0, msg);

    // create helper process and pass p1[1] across to it
    const char* argv[] = { "/boot/bin/reply-handle-helper" };
    uint32_t id = MX_HND_INFO(MX_HND_TYPE_USER0, 0);
    p = launchpad_launch_mxio_etc(argv[0], 1, argv, NULL, 1, &p1[1], &id);
    snprintf(msg, sizeof(msg), "launchpad_launch_mxio_etc failed: %d\n", p);
    ASSERT_GT(p, 0, msg);

    mx_signals_state_t pending;
    r = mx_handle_wait_one(p2[0], MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending);
    snprintf(msg, sizeof(msg), "error waiting on p2[0] %d\n", r);
    ASSERT_GE(r, 0, msg);

    ASSERT_TRUE(pending.satisfied & MX_SIGNAL_READABLE, "channel 2a not readable");

    unittest_printf("write handle %x to helper...\n", p2[1]);
    char data[128];
    mx_handle_t h;
    uint32_t dsz = sizeof(data) - 1;
    uint32_t hsz = 1;
    r = mx_channel_read(p2[0], 0, data, dsz, &dsz, &h, hsz, &hsz);
    snprintf(msg, sizeof(msg), "failed to read reply %d\n", r);
    ASSERT_GE(r, 0, msg);

    data[dsz] = 0;
    unittest_printf("reply: '%s' %u %u\n", data, dsz, hsz);
    ASSERT_EQ(hsz, 1u, "no handle returned");

    unittest_printf("read handle %x from reply port\n", h);
    ASSERT_EQ(h, p2[1], "different handle returned");

    END_TEST;
}

BEGIN_TEST_CASE(reply_handle_tests)
RUN_TEST(reply_handle_basic)
RUN_TEST(reply_handle_rw)
END_TEST_CASE(reply_handle_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
