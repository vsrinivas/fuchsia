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

    mx_handle_t io_port;
    status = mx_port_create(0u, &io_port);
    EXPECT_EQ(status, 0, "could not create ioport");

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

    EXPECT_EQ(memcmp(&in.payload, &out.payload, 8u), 0, "data must be the same");

    status = mx_handle_close(io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");

    END_TEST;
}

static bool queue_and_close_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t io_port;
    status = mx_port_create(0u, &io_port);
    EXPECT_EQ(status, 0, "could not create ioport");

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

    status = mx_port_create(0u, &tinfo.io_port);
    EXPECT_EQ(status, 0, "could not create ioport");

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

    mx_handle_t ioport;
    status = mx_port_create(0u, &ioport);
    EXPECT_EQ(status, 0, "could not create io port");

    mx_handle_t channel[2];
    status = mx_channel_create(0u, channel, channel + 1);
    EXPECT_EQ(status, NO_ERROR, "could not create channel");

    mx_handle_t event;
    status = mx_port_create(0u, &event);
    EXPECT_EQ(status, 0, "could not create io port");

    status = mx_port_bind(ioport, -1, event, MX_SIGNAL_SIGNALED);
    EXPECT_EQ(status, ERR_NOT_SUPPORTED, "non waitable objects not allowed");

    status = mx_port_bind(ioport, -1, channel[0], MX_SIGNAL_READABLE);
    EXPECT_EQ(status, NO_ERROR, "failed to bind channel");

    status = mx_port_bind(ioport, -2, channel[1], MX_SIGNAL_READABLE);
    EXPECT_EQ(status, NO_ERROR, "failed to bind channel");

    status = mx_handle_close(ioport);
    EXPECT_EQ(status, NO_ERROR, "failed to close io port");

    status = mx_handle_close(channel[0]);
    EXPECT_EQ(status, NO_ERROR, "failed to close channel");

    status = mx_handle_close(channel[1]);
    EXPECT_EQ(status, NO_ERROR, "failed to close channel");

    status = mx_handle_close(event);
    EXPECT_EQ(status, NO_ERROR, "failed to close event");

    END_TEST;
}

typedef struct io_info {
    int count;
    volatile mx_status_t error;
    mx_handle_t io_port;
    mx_handle_t reply_channel;
} io_info_t;

typedef struct report {
    uint64_t key;
    uint64_t type;
    uint32_t size;
    mx_signals_t signals;
} report_t;

static int io_reply_thread(void* arg)
{
    io_info_t* info = arg;
    info->error = 0;

    mx_io_packet_t io_pkt;
    mx_status_t status;

    // Wait for the other thread to poke at the events and send each key/signal back to
    // the thread via a channel.
    for (int ix = 0; ix != info->count; ++ix) {
        status = mx_port_wait(info->io_port, &io_pkt, sizeof(io_pkt));

        if (status != NO_ERROR) {
            info->error = status;
            break;
        }

        report_t report = { io_pkt.hdr.key, io_pkt.hdr.type, io_pkt.bytes, io_pkt.signals };
        status = mx_channel_write(info->reply_channel, 0u, &report, sizeof(report), NULL, 0);
        if (status != NO_ERROR) {
            info->error = status;
            break;
        }
    }

    return 0;
}

static bool bind_channels_test(void)
{
    BEGIN_TEST;
    mx_status_t status;
    io_info_t info = {0};

    status = mx_port_create(0u, &info.io_port);
    EXPECT_EQ(status, 0, "could not create ioport");

    mx_handle_t h[2];
    status = mx_channel_create(0, h, h + 1);
    EXPECT_EQ(status, 0, "could not create channels");

    mx_handle_t recv_channel = h[0];
    info.reply_channel = h[1];

    // Poke at the channels in some order. Note that we bound the even channels so we
    // write to the odd ones.
    int order[] = {1, 3, 3, 1, 5, 7, 1, 5, 3, 3, 3, 9};
    info.count = countof(order);

    mx_handle_t channels[10];
    for (int ix = 0; ix != countof(channels) / 2; ++ix) {
        status = mx_channel_create(0u, &channels[ix * 2], &channels[ix * 2 + 1]);
        EXPECT_EQ(status, NO_ERROR, "failed to create channel");
        status = mx_port_bind(info.io_port, (ix * 2) + 1, channels[ix * 2], MX_SIGNAL_READABLE);
        EXPECT_EQ(status, NO_ERROR, "failed to bind event to ioport");
    }

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, io_reply_thread, &info, "reply1");
    EXPECT_EQ(ret, thrd_success, "could not create thread");

    char msg[] = "=msg0=";

    struct pair { int actual; int expected; };
    struct pair arrivals[10] = {0};

    for (int ix = 0; ix != countof(order); ++ix) {
        msg[4] = (char)ix;
        status = mx_channel_write(channels[order[ix]], 0u, msg, sizeof(msg), NULL, 0);
        EXPECT_EQ(status, NO_ERROR, "could not signal");
        ++arrivals[order[ix]].expected;
    }

    report_t report;
    uint32_t bytes = sizeof(report);

    // Check the received packets are reasonable.
    for (int ix = 0; ix != countof(order); ++ix) {
        status = mx_handle_wait_one(recv_channel, MX_SIGNAL_READABLE, MX_TIME_INFINITE, NULL);
        EXPECT_EQ(status, NO_ERROR, "failed to wait for channel");
        status = mx_channel_read(recv_channel, 0u, &report, bytes, &bytes, NULL, 0, NULL);
        EXPECT_EQ(status, NO_ERROR, "expected valid message");
        EXPECT_EQ(report.signals, MX_SIGNAL_READABLE, "invalid signal");
        EXPECT_EQ(report.type, MX_PORT_PKT_TYPE_IOSN, "invalid type");
        ++arrivals[(int)report.key].actual;
    }

    // Check that all messages arrived, even though the relative order might be
    // different.
    for (int ix = 0; ix != countof(arrivals); ++ix) {
        EXPECT_EQ(arrivals[ix].actual, arrivals[ix].expected, "missing packet");
    }

    ret = thrd_join(thread, NULL);
    EXPECT_EQ(ret, thrd_success, "could not wait for thread");

    // Test cleanup.
    for (int ix = 0; ix != countof(channels); ++ix) {
        status = mx_handle_close(channels[ix]);
        EXPECT_EQ(status, NO_ERROR, "failed closing events");
    }

    status = mx_handle_close(info.io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");
    status = mx_handle_close(info.reply_channel);
    EXPECT_EQ(status, NO_ERROR, "failed to close channel 0");
    status = mx_handle_close(recv_channel);
    EXPECT_EQ(status, NO_ERROR, "failed to close channel 1");

    END_TEST;
}

static bool bind_sockets_test(void)
{
    BEGIN_TEST;
    mx_status_t status;
    mx_size_t sz;

    mx_handle_t io_port;
    status = mx_port_create(0u, &io_port);
    EXPECT_EQ(status, 0, "");

    mx_handle_t socket0, socket1;
    status = mx_socket_create(0u, &socket0, &socket1);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_port_bind(io_port, 1ull, socket1, MX_SIGNAL_READABLE | MX_USER_SIGNAL_3);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_socket_write(socket0, 0u, "ab", 2, &sz);
    EXPECT_EQ(status, NO_ERROR, "");
    EXPECT_EQ(sz, 2u, "");
    status = mx_socket_write(socket0, 0u, "bc", 2, &sz);
    EXPECT_EQ(status, NO_ERROR, "");
    EXPECT_EQ(sz, 2u, "");

    mx_handle_t channel[2];
    status = mx_channel_create(0u, channel, channel + 1);
    EXPECT_EQ(status, NO_ERROR, "");

    io_info_t info = {2, 0, io_port, channel[1]};

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, io_reply_thread, &info, "reply2");
    EXPECT_EQ(ret, thrd_success, "");

    report_t report;
    uint32_t bytes = sizeof(report);

    for (int ix = 0; ix != 2; ++ix) {
        status = mx_handle_wait_one(channel[0], MX_SIGNAL_READABLE, MX_TIME_INFINITE, NULL);
        EXPECT_EQ(status, NO_ERROR, "");
        status = mx_channel_read(channel[0], 0u, &report, bytes, &bytes, NULL, 0, NULL);
        EXPECT_EQ(status, NO_ERROR, "");
        EXPECT_EQ(report.signals, MX_SIGNAL_READABLE, "");
        EXPECT_EQ(report.type, MX_PORT_PKT_TYPE_IOSN, "");
        // TODO(cpu): No longer we return the size. It seems we can get this back.
        EXPECT_EQ(report.size, 0u, "");
    }

    ret = thrd_join(thread, NULL);
    EXPECT_EQ(ret, thrd_success, "");

    mx_io_packet_t io_pkt = {0};
    status = mx_object_signal(socket0, 0u, MX_USER_SIGNAL_3);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_port_wait(io_port, &io_pkt, sizeof(io_pkt));
    EXPECT_EQ(status, NO_ERROR, "");
    EXPECT_EQ(io_pkt.signals, MX_USER_SIGNAL_3, "");

    status = mx_handle_close(io_port);
    EXPECT_EQ(status, NO_ERROR, "");
    status = mx_handle_close(socket0);
    EXPECT_EQ(status, NO_ERROR, "");
    status = mx_handle_close(socket1);
    EXPECT_EQ(status, NO_ERROR, "");

    END_TEST;
}

static bool bind_channels_playback(void)
{
    BEGIN_TEST;
    mx_status_t status;
    mx_handle_t port;
    mx_handle_t h[2];

    status = mx_port_create(0u, &port);
    EXPECT_EQ(status, 0, "");

    status = mx_channel_create(0, h, h + 1);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_channel_write(h[0], 0u, "abcd", 4, NULL, 0);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_channel_write(h[0], 0u, "def", 3, NULL, 0);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_port_bind(port, 3ull, h[1], MX_SIGNAL_READABLE);
    EXPECT_EQ(status, NO_ERROR, "");

    mx_io_packet_t io_pkt;
    for (int ix = 0; ix != 2; ++ix) {
        status = mx_port_wait(port, &io_pkt, sizeof(io_pkt));
        EXPECT_EQ(status, NO_ERROR, "");
        EXPECT_EQ(io_pkt.signals, MX_SIGNAL_READABLE, "");
    }

    status = mx_handle_close(port);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_handle_close(h[0]);
    EXPECT_EQ(status, NO_ERROR, "");

    status = mx_handle_close(h[1]);
    EXPECT_EQ(status, NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(io_port_tests)
RUN_TEST(basic_test)
RUN_TEST(queue_and_close_test)
RUN_TEST(thread_pool_test)
RUN_TEST(bind_basic_test)
RUN_TEST(bind_channels_test)
RUN_TEST(bind_sockets_test)
RUN_TEST(bind_channels_playback)
END_TEST_CASE(io_port_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
