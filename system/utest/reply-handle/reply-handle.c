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

#include <magenta/syscalls.h>
#include <mxu/unittest.h>

#include <mxio/io.h>
#include <mxio/util.h>

int reply_handle_test(void) {
    BEGIN_TEST;

    mx_handle_t p1a, p1b, p2a, p2b;
    uintptr_t entry;
    mx_handle_t p;
    mx_status_t r;
    int fd;
    char msg[128];

    p1a = _magenta_message_pipe_create(&p1b);
    snprintf(msg, sizeof(msg), "failed to create pipe1 %d\n", p1a);
    ASSERT_GE(p1a, 0, msg);

    p2a = _magenta_message_pipe_create(&p2b);
    snprintf(msg, sizeof(msg), "failed to create pipe2 %d\n", p2a);
    ASSERT_GE(p2a, 0, msg);

    // send a message and p2b through p1a
    r = _magenta_message_write(p1a, "hello", 6, &p2b, 1, 0);
    snprintf(msg, sizeof(msg), "failed to write message+handle to p1a %d\n", r);
    EXPECT_GE(r, 0, msg);

    // create helper process and pass p1b across to it
    p = _magenta_process_create("helper", 7);
    snprintf(msg, sizeof(msg), "couldn't create process %d\n", p);
    ASSERT_GE(p, 0, msg);

    fd = open("/boot/bin/reply-handle-helper", O_RDONLY);
    snprintf(msg, sizeof(msg), "couldn't open reply-handle-helper %d\n", fd);
    ASSERT_GE(fd, 0, msg);

    r = mxio_load_elf_fd(p, &entry, fd);
    snprintf(msg, sizeof(msg), "couldn't load reply-handle-helper %d\n", r);
    ASSERT_GE(r, 0, msg);

    r = _magenta_process_start(p, p1b, entry);
    snprintf(msg, sizeof(msg), "process did not start %d\n", r);
    ASSERT_GE(r, 0, msg);

    mx_signals_t pending;
    r = _magenta_handle_wait_one(p2a, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                 MX_TIME_INFINITE, &pending, NULL);
    snprintf(msg, sizeof(msg), "error waiting on p2a %d\n", r);
    ASSERT_GE(r, 0, msg);

    ASSERT_TRUE(pending & MX_SIGNAL_READABLE, "pipe 2a not readable");

    unittest_printf("write handle %x to helper...\n", p2b);
    char data[128];
    mx_handle_t h;
    uint32_t dsz = sizeof(data) - 1;
    uint32_t hsz = 1;
    r = _magenta_message_read(p2a, data, &dsz, &h, &hsz, 0);
    snprintf(msg, sizeof(msg), "failed to read reply %d\n", r);
    ASSERT_GE(r, 0, msg);

    data[dsz] = 0;
    unittest_printf("reply: '%s' %u %u\n", data, dsz, hsz);
    ASSERT_EQ(hsz, 1u, "no handle returned");

    unittest_printf("read handle %x from reply port\n", h);
    ASSERT_EQ(h, p2b, "different handle returned");

    END_TEST;
}

BEGIN_TEST_CASE(reply_handle_tests)
RUN_TEST(reply_handle_test)
END_TEST_CASE(reply_handle_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
