// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#define NUM_IO_THREADS 5
#define NUM_SLOTS 10

typedef struct mx_user_packet {
    mx_packet_header_t hdr;
    uint64_t param[8];
} mx_user_packet_t;

typedef struct t_info {
    volatile mx_status_t error;
    mx_handle_t io_port;
    uintptr_t work_count[NUM_SLOTS];
} t_info_t;

static int thread_consumer(void* arg)
{
    t_info_t* tinfo = arg;

    tinfo->error = 0;

    mx_user_packet_t us_pkt;
    mx_status_t status;

    while (true) {
        status = mx_port_wait(tinfo->io_port, &us_pkt, sizeof(us_pkt));

        if (status < 0) {
            tinfo->error = status;
            break;
        } else if (us_pkt.hdr.key >= NUM_SLOTS) {
            // expected termination.
            break;
        }

        tinfo->work_count[(int)us_pkt.hdr.key] += us_pkt.param[0];
        mx_nanosleep(1u);
    };

    return 0;
}

static bool basic_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t io_port = mx_port_create(0u);
    EXPECT_GT(io_port, 0, "could not create ioport");

    typedef struct {
        mx_packet_header_t hdr;
        char payload[8];
    } packet_t;

    const packet_t in = {{33u, 255u, 10u}, {164, 5, 7, 9, 99, 253, 1, 66}};
    packet_t out = {0};

    status = mx_port_queue(io_port, &in, 8u);
    EXPECT_EQ(status, ERR_INVALID_ARGS, "expected failure");

    status = mx_port_queue(io_port, &in, sizeof(in));
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_port_wait(io_port, &out, sizeof(out));
    EXPECT_EQ(status, NO_ERROR, "");

    EXPECT_EQ(out.hdr.key, 33u, "key mismatch");
    EXPECT_EQ(out.hdr.type, MX_PORT_PKT_TYPE_USER, "type mismatch");
    EXPECT_EQ(out.hdr.extra, 10u, "key mismatch");

    //EXPECT_EQ(memcmp(&in.payload, &out.payload, 8u), 0, "data must be the same");

    status = mx_handle_close(io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");

    END_TEST;
}

static bool queue_and_close_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t io_port = mx_port_create(0u);
    EXPECT_GT(io_port, 0, "could not create ioport");

    typedef struct {
        mx_packet_header_t hdr;
        int x;
    } packet_t;

    const packet_t in = {{1u, 2u, 3u}, -1};

    status = mx_port_queue(io_port, &in, sizeof(in));
    EXPECT_EQ(status, NO_ERROR, "expected failure");

    status = mx_handle_close(io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");

    END_TEST;
}

static bool thread_pool_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    t_info_t tinfo = {0u, 0, {0}};

    tinfo.io_port = mx_port_create(0u);
    EXPECT_GT(tinfo.io_port, 0, "could not create ioport");

    thrd_t threads[NUM_IO_THREADS];
    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        int ret = thrd_create_with_name(&threads[ix], thread_consumer, &tinfo, "tpool");
        EXPECT_EQ(ret, thrd_success, "could not create thread");
    }

    mx_user_packet_t us_pkt = {0};

    for (size_t ix = 0; ix != NUM_SLOTS + NUM_IO_THREADS; ++ix) {
        us_pkt.hdr.key = ix;
        us_pkt.param[0] = 10 + ix;
        mx_port_queue(tinfo.io_port, &us_pkt, sizeof(us_pkt));
    }

    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        int ret = thrd_join(threads[ix], NULL);
        EXPECT_EQ(ret, NO_ERROR, "failed to wait");
    }

    EXPECT_EQ(tinfo.error, NO_ERROR, "thread faulted somewhere");

    status = mx_handle_close(tinfo.io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");

    int sum = 0;
    for (size_t ix = 0; ix != NUM_SLOTS; ++ix) {
        int slot = tinfo.work_count[ix];
        EXPECT_GT(slot, 0, "bad slot entry");
        sum += slot;
    }
    EXPECT_EQ(sum, 145, "bad sum");

    END_TEST;
}

static bool bind_basic_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t ioport = mx_port_create(0u);
    EXPECT_GT(ioport, 0, "could not create io port");

    mx_handle_t pipe[2];
    status = mx_msgpipe_create(pipe, 0u);
    EXPECT_EQ(status, NO_ERROR, "could not create pipe");

    mx_handle_t event = mx_port_create(0u);
    EXPECT_GT(event, 0, "could not create io port");

    status = mx_port_bind(ioport, -1, event, MX_SIGNAL_SIGNALED);
    EXPECT_EQ(status, ERR_NOT_SUPPORTED, "non waitable objects not allowed");

    status = mx_port_bind(ioport, -1, pipe[0], MX_SIGNAL_READABLE);
    EXPECT_EQ(status, NO_ERROR, "failed to bind pipe");

    status = mx_port_bind(ioport, -2, pipe[1], MX_SIGNAL_READABLE);
    EXPECT_EQ(status, NO_ERROR, "failed to bind pipe");

    status = mx_handle_close(ioport);
    EXPECT_EQ(status, NO_ERROR, "failed to close io port");

    status = mx_handle_close(pipe[0]);
    EXPECT_EQ(status, NO_ERROR, "failed to close pipe");

    status = mx_handle_close(pipe[1]);
    EXPECT_EQ(status, NO_ERROR, "failed to close pipe");

    status = mx_handle_close(event);
    EXPECT_EQ(status, NO_ERROR, "failed to close event");

    END_TEST;
}

typedef struct io_info {
    volatile mx_status_t error;
    mx_handle_t io_port;
    mx_handle_t reply_pipe;
} io_info_t;

typedef struct report {
    uint64_t key;
    uint64_t type;
    mx_signals_t signals;
} report_t;

static int io_reply_thread(void* arg)
{
    io_info_t* info = arg;
    info->error = 0;

    mx_io_packet_t io_pkt;
    mx_status_t status;

    // Wait for the other thread to poke at the events and send each key/signal back to
    // the thread via a message pipe.
    while (true) {
        status = mx_port_wait(info->io_port, &io_pkt, sizeof(io_pkt));
        if (status != NO_ERROR) {
            info->error = status;
            break;
        }
        if (io_pkt.hdr.key == 0) {
            // Normal exit.
            break;
        }

        report_t report = { io_pkt.hdr.key, io_pkt.hdr.type, io_pkt.signals };
        status = mx_msgpipe_write(info->reply_pipe, &report, sizeof(report), NULL, 0, 0u);
        if (status != NO_ERROR) {
            info->error = status;
            break;
        }

    };

    return 0;
}

static bool bind_pipes_test(void)
{
    BEGIN_TEST;
    mx_status_t status;
    io_info_t info = {0};

    info.io_port = mx_port_create(0u);
    EXPECT_GT(info.io_port, 0, "could not create ioport");

    mx_handle_t h[2];
    status = mx_msgpipe_create(h, 0);
    EXPECT_EQ(status, 0, "could not create pipes");

    mx_handle_t recv_pipe = h[0];
    info.reply_pipe = h[1];

    mx_handle_t pipes[10];
    for (int ix = 0; ix != countof(pipes) / 2; ++ix) {
        status = mx_msgpipe_create(&pipes[ix * 2], 0u);
        EXPECT_EQ(status, NO_ERROR, "failed to create pipe");
        status = mx_port_bind(info.io_port, (ix * 2) + 1, pipes[ix * 2], MX_SIGNAL_READABLE);
        EXPECT_EQ(status, NO_ERROR, "failed to bind event to ioport");
    }

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, io_reply_thread, &info, "reply");
    EXPECT_EQ(ret, thrd_success, "could not create thread");

    char msg[] = "=msg0=";

    // Poke at the pipes in some order, mesages with the events should arrive in order.
    // note that we bound the even pipes so we write to the odd ones.
    int order[] = {1, 3, 3, 1, 5, 7, 1, 5, 3, 3, 3, 9};
    for (int ix = 0; ix != countof(order); ++ix) {
        msg[4] = (char)ix;
        status = mx_msgpipe_write(pipes[order[ix]], msg, sizeof(msg), NULL, 0, 0u);
        EXPECT_EQ(status, NO_ERROR, "could not signal");
    }

    // Queue a final packet to make io_reply_thread exit.
    mx_io_packet_t io_pkt = {0};
    status = mx_port_queue(info.io_port, &io_pkt, sizeof(io_pkt));

    report_t report;
    uint32_t bytes = sizeof(report);

    // The messages should match the pipe poke order.
    for (int ix = 0; ix != countof(order); ++ix) {
        status = mx_handle_wait_one(recv_pipe, MX_SIGNAL_READABLE, 1000000000ULL, NULL);
        EXPECT_EQ(status, NO_ERROR, "failed to wait for pipe");
        status = mx_msgpipe_read(recv_pipe, &report, &bytes, NULL, NULL, 0u);
        EXPECT_EQ(status, NO_ERROR, "expected valid message");
        EXPECT_EQ(report.signals, MX_SIGNAL_READABLE, "invalid signal");
        EXPECT_EQ(report.type, MX_PORT_PKT_TYPE_IOSN, "invalid type");
        EXPECT_EQ(report.key, (unsigned long long)order[ix], "wrong order");
    }

    ret = thrd_join(thread, NULL);
    EXPECT_EQ(ret, thrd_success, "could not wait for thread");

    // Test cleanup.
    for (int ix = 0; ix != countof(pipes); ++ix) {
        status = mx_handle_close(pipes[ix]);
        EXPECT_EQ(status, NO_ERROR, "failed closing events");
    }

    status = mx_handle_close(info.io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");
    status = mx_handle_close(info.reply_pipe);
    EXPECT_EQ(status, NO_ERROR, "failed to close pipe 0");
    status = mx_handle_close(recv_pipe);
    EXPECT_EQ(status, NO_ERROR, "failed to close pipe 1");

    END_TEST;
}

BEGIN_TEST_CASE(io_port_tests)
RUN_TEST(basic_test)
RUN_TEST(queue_and_close_test)
RUN_TEST(thread_pool_test)
RUN_TEST(bind_basic_test)
RUN_TEST(bind_pipes_test)
END_TEST_CASE(io_port_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
