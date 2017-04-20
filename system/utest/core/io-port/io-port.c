// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <unittest/unittest.h>

#define NUM_IO_THREADS 5
#define NUM_SLOTS 10

typedef struct mx_user_packet {
    mx_packet_header_t hdr;
    uint64_t param[8];
} mx_user_packet_t;

typedef struct t_info {
    volatile mx_status_t error;
    mx_handle_t port;
    uintptr_t work_count[NUM_SLOTS];
} t_info_t;

static int thread_consumer(void* arg)
{
    t_info_t* tinfo = arg;

    tinfo->error = 0;

    mx_user_packet_t us_pkt;
    mx_status_t status;

    while (true) {
        status = mx_port_wait(tinfo->port, MX_TIME_INFINITE, &us_pkt, sizeof(us_pkt));

        if (status < 0) {
            tinfo->error = status;
            break;
        } else if (us_pkt.hdr.key >= NUM_SLOTS) {
            // expected termination.
            break;
        }

        tinfo->work_count[(int)us_pkt.hdr.key] += us_pkt.param[0];
        mx_nanosleep(mx_deadline_after(1u));
    };

    return 0;
}

static bool basic_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t port;
    status = mx_port_create(0u, &port);
    EXPECT_EQ(status, 0, "could not create ioport");

    typedef struct {
        mx_packet_header_t hdr;
        char payload[8];
    } packet_t;

    const packet_t in = {{33u, 255u, 10u}, {164, 5, 7, 9, 99, 253, 1, 66}};
    packet_t out = {};

    status = mx_port_queue(port, &in, 8u);
    EXPECT_EQ(status, MX_ERR_INVALID_ARGS, "expected failure");

    status = mx_port_queue(port, &in, sizeof(in));
    EXPECT_EQ(status, MX_OK, "");

    status = mx_port_wait(port, MX_TIME_INFINITE, &out, sizeof(out));
    EXPECT_EQ(status, MX_OK, "");

    EXPECT_EQ(out.hdr.key, 33u, "key mismatch");
    EXPECT_EQ(out.hdr.type, MX_PORT_PKT_TYPE_USER, "type mismatch");
    EXPECT_EQ(out.hdr.extra, 10u, "");

    EXPECT_EQ(memcmp(&in.payload, &out.payload, 8u), 0, "data must be the same");

    status = mx_handle_close(port);
    EXPECT_EQ(status, MX_OK, "failed to close ioport");

    END_TEST;
}

static bool queue_and_close_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t port;
    status = mx_port_create(0u, &port);
    EXPECT_EQ(status, 0, "could not create ioport");

    typedef struct {
        mx_packet_header_t hdr;
        int x;
    } packet_t;

    const packet_t in = {{1u, 2u, 3u}, -1};

    status = mx_port_queue(port, &in, sizeof(in));
    EXPECT_EQ(status, MX_OK, "expected failure");

    status = mx_handle_close(port);
    EXPECT_EQ(status, MX_OK, "failed to close ioport");

    END_TEST;
}

static bool thread_pool_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    t_info_t tinfo = {0u, 0, {0}};

    status = mx_port_create(0u, &tinfo.port);
    EXPECT_EQ(status, 0, "could not create ioport");

    thrd_t threads[NUM_IO_THREADS];
    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        int ret = thrd_create_with_name(&threads[ix], thread_consumer, &tinfo, "tpool");
        EXPECT_EQ(ret, thrd_success, "could not create thread");
    }

    mx_user_packet_t us_pkt = {};

    for (size_t ix = 0; ix != NUM_SLOTS + NUM_IO_THREADS; ++ix) {
        us_pkt.hdr.key = ix;
        us_pkt.param[0] = 10 + ix;
        mx_port_queue(tinfo.port, &us_pkt, sizeof(us_pkt));
    }

    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        int ret = thrd_join(threads[ix], NULL);
        EXPECT_EQ(ret, MX_OK, "failed to wait");
    }

    EXPECT_EQ(tinfo.error, MX_OK, "thread faulted somewhere");

    status = mx_handle_close(tinfo.port);
    EXPECT_EQ(status, MX_OK, "failed to close ioport");

    int sum = 0;
    for (size_t ix = 0; ix != NUM_SLOTS; ++ix) {
        int slot = tinfo.work_count[ix];
        EXPECT_GT(slot, 0, "bad slot entry");
        sum += slot;
    }
    EXPECT_EQ(sum, 145, "bad sum");

    END_TEST;
}

typedef struct io_info {
    int count;
    volatile mx_status_t error;
    mx_handle_t port;
    mx_handle_t reply_channel;
} io_info_t;

typedef struct report {
    uint64_t key;
    uint64_t type;
    uint32_t size;
    mx_signals_t signals;
} report_t;

static bool port_timeout(void) {
    BEGIN_TEST;
    mx_status_t status;
    mx_handle_t port;

    status = mx_port_create(0u, &port);
    EXPECT_EQ(status, 0, "");

    mx_user_packet_t out = {};
    status = mx_port_wait(port, mx_deadline_after(MX_MSEC(5)), &out, sizeof(out));
    EXPECT_EQ(status, MX_ERR_TIMED_OUT, "");

    const mx_user_packet_t in = {{5u, 6u, 7u}, {}};
    status = mx_port_queue(port, &in, sizeof(in));
    EXPECT_EQ(status, MX_OK, "");

    status = mx_port_wait(port, 0ull, &out, sizeof(out));
    EXPECT_EQ(status, MX_OK, "");

    EXPECT_EQ(out.hdr.key, 5u, "");
    EXPECT_EQ(out.hdr.type, MX_PORT_PKT_TYPE_USER, "");
    EXPECT_EQ(out.hdr.extra, 7u, "");

    status = mx_handle_close(port);
    EXPECT_EQ(status, MX_OK, "");

    END_TEST;
}

BEGIN_TEST_CASE(port_tests)
RUN_TEST(basic_test)
RUN_TEST(queue_and_close_test)
RUN_TEST(thread_pool_test)
RUN_TEST(port_timeout)
END_TEST_CASE(port_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
