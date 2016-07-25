// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

int reply_handle_basic(void) {
    BEGIN_TEST;

    mx_handle_t p1[2], p2[2];
    mx_status_t r;

    r = mx_message_pipe_create(p1, 0);
    ASSERT_EQ(r, NO_ERROR, "failed to create pipe p1");

    r = mx_message_write(p1[0], "hello", 6, &p1[0], 1, 0);
    ASSERT_EQ(r, ERR_NOT_SUPPORTED, "expected failure");

    r = mx_message_write(p1[1], "hello", 6, &p1[1], 1, 0);
    ASSERT_EQ(r, ERR_NOT_SUPPORTED, "expected failure");

    r = mx_message_pipe_create(p2, MX_FLAG_REPLY_PIPE);
    ASSERT_EQ(r, NO_ERROR, "failed to create pipe p2");

    r = mx_message_write(p2[1], "hello", 6, NULL, 0, 0);
    ASSERT_EQ(r, ERR_BAD_STATE, "expected failure");

    r = mx_message_write(p2[1], "hello", 6, &p1[1], 1, 0);
    ASSERT_EQ(r, ERR_BAD_STATE, "expected failure");

    mx_handle_t har1[2] = {p2[1], p1[1]};
    r = mx_message_write(p2[1], "hello", 6, har1, 2, 0);
    ASSERT_EQ(r, ERR_BAD_STATE, "expected failure");

    mx_handle_t har2[2] = {p1[1], p2[1]};
    r = mx_message_write(p2[1], "hello", 6, har2, 2, 0);
    ASSERT_EQ(r, NO_ERROR, "expected success");

    END_TEST;
}

int reply_handle_rw(void) {
    BEGIN_TEST;

    mx_handle_t p1[2], p2[2];
    uintptr_t entry;
    mx_handle_t p;
    mx_status_t r;
    char msg[128];

    r = mx_message_pipe_create(p1, 0);
    snprintf(msg, sizeof(msg), "failed to create pipe1 %d\n", r);
    ASSERT_EQ(r, 0, msg);

    r = mx_message_pipe_create(p2, MX_FLAG_REPLY_PIPE);
    snprintf(msg, sizeof(msg), "failed to create pipe2 %d\n", r);
    ASSERT_EQ(r, 0, msg);

    // send a message and p2[1] through p1[0]
    r = mx_message_write(p1[0], "hello", 6, &p2[1], 1, 0);
    snprintf(msg, sizeof(msg), "failed to write message+handle to p1[0] %d\n", r);
    EXPECT_GE(r, 0, msg);

    // create helper process and pass p1[1] across to it
    launchpad_t* lp;
    r = launchpad_create("helper", &lp);
    snprintf(msg, sizeof(msg), "launchpad_create failed: %d\n", r);
    ASSERT_EQ(r, 0, msg);

    mx_handle_t vmo = launchpad_vmo_from_file("/boot/bin/reply-handle-helper");
    snprintf(msg, sizeof(msg), "couldn't open reply-handle-helper %d\n", vmo);
    ASSERT_GT(vmo, 0, msg);

    r = launchpad_elf_load_basic(lp, vmo);
    snprintf(msg, sizeof(msg), "couldn't load reply-handle-helper %d\n", r);
    ASSERT_EQ(r, 0, msg);

    r = launchpad_get_entry_address(lp, &entry);
    ASSERT_EQ(r, 0, "launchpad_get_entry_address failed");

    p = launchpad_get_process_handle(lp);
    ASSERT_GT(p, 0, "launchpad_get_process_handle failed");

    r = mx_process_start(p, p1[1], entry);
    snprintf(msg, sizeof(msg), "process did not start %d\n", r);
    ASSERT_GE(r, 0, msg);

    launchpad_destroy(lp);

    mx_signals_state_t pending;
    r = mx_handle_wait_one(p2[0], MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending);
    snprintf(msg, sizeof(msg), "error waiting on p2[0] %d\n", r);
    ASSERT_GE(r, 0, msg);

    ASSERT_TRUE(pending.satisfied & MX_SIGNAL_READABLE, "pipe 2a not readable");

    unittest_printf("write handle %x to helper...\n", p2[1]);
    char data[128];
    mx_handle_t h;
    uint32_t dsz = sizeof(data) - 1;
    uint32_t hsz = 1;
    r = mx_message_read(p2[0], data, &dsz, &h, &hsz, 0);
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
