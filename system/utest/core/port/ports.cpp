// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <unittest/unittest.h>

static bool basic_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t port;
    status = mx_port_create(MX_PORT_OPT_V2, &port);
    EXPECT_EQ(status, 0, "could not create port v2");

    const mx_port_packet_t in = {
        12ull,
        MX_PKT_TYPE_USER + 5u,    // kernel overrides the |type|.
        -3,
        { {} }
    };

    mx_port_packet_t out = {};

    status = mx_port_queue(port, nullptr, 0u);
    EXPECT_EQ(status, ERR_INVALID_ARGS, "");

    status = mx_port_queue(port, &in, 0u);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_port_wait(port, MX_TIME_INFINITE, &out, 0u);
    EXPECT_EQ(status, NO_ERROR, "");

    EXPECT_EQ(out.key, 12u, "");
    EXPECT_EQ(out.type, MX_PKT_TYPE_USER, "");
    EXPECT_EQ(out.status, -3, "");

    EXPECT_EQ(memcmp(&in.user, &out.user, sizeof(mx_port_packet_t::user)), 0, "");

    status = mx_handle_close(port);
    EXPECT_EQ(status, NO_ERROR, "");

    END_TEST;
}

static bool queue_and_close_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t port;
    status = mx_port_create(MX_PORT_OPT_V2, &port);
    EXPECT_EQ(status, 0, "could not create port v2");

    const mx_port_packet_t in = {
        1ull,
        MX_PKT_TYPE_USER,
        0,
        { {} }
    };

    status = mx_port_queue(port, &in, 0u);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_handle_close(port);
    EXPECT_EQ(status, NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(port_tests)
RUN_TEST(basic_test)
RUN_TEST(queue_and_close_test)
END_TEST_CASE(port_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
