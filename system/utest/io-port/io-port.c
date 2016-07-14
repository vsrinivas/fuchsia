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

#include <stdio.h>

#include <magenta/syscalls.h>
#include <runtime/thread.h>
#include <unittest/unittest.h>

#define NUM_IO_THREADS 5
#define NUM_SLOTS 10

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
        status = mx_io_port_wait(tinfo->io_port, &us_pkt, sizeof(us_pkt));

        if (status < 0) {
            tinfo->error = status;
            break;
        } else if (us_pkt.key >= NUM_SLOTS) {
            // expected termination.
            break;
        }

        tinfo->work_count[(int)us_pkt.key] += us_pkt.param[0];
        mx_nanosleep(1u);
    };

    return 0;
}

static bool basic_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t io_port = mx_io_port_create(0u);
    EXPECT_GT(io_port, 0, "could not create ioport");

    mx_user_packet_t us_pkt = {0};

    status = mx_io_port_queue(io_port, &us_pkt, 8u);
    EXPECT_EQ(status, ERR_INVALID_ARGS, "expected failure");

    status = mx_io_port_wait(io_port, &us_pkt, 8u);
    EXPECT_EQ(status, ERR_INVALID_ARGS, "expected failure");

    int slots = 0;

    while (true) {
        us_pkt.key = 128 - slots;
        status = mx_io_port_queue(io_port, &us_pkt, sizeof(us_pkt));
        if (status == ERR_NOT_ENOUGH_BUFFER)
            break;
        EXPECT_EQ(status, NO_ERROR, "could not queue");
        ++slots;
    }

    EXPECT_EQ(slots, 128, "incorrect number of slots");

    status = mx_io_port_wait(io_port, &us_pkt, sizeof(us_pkt));
    EXPECT_EQ(status, NO_ERROR, "failed to dequeue");
    EXPECT_EQ(us_pkt.key, 128U, "wrong key");

    status = mx_handle_close(io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");

    END_TEST;
}

static bool thread_pool_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    t_info_t tinfo = {0u, 0, {0}};

    tinfo.io_port = mx_io_port_create(0u);
    EXPECT_GT(tinfo.io_port, 0, "could not create ioport");

    mxr_thread_t *threads[NUM_IO_THREADS];
    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        status = mxr_thread_create(thread_consumer, &tinfo, "tpool", &threads[ix]);
        EXPECT_EQ(status, 0, "could not create thread");
    }

    mx_user_packet_t us_pkt = {0};

    for (size_t ix = 0; ix != NUM_SLOTS + NUM_IO_THREADS; ++ix) {
        us_pkt.key = ix;
        us_pkt.param[0] = 10 + ix;
        mx_io_port_queue(tinfo.io_port, &us_pkt, sizeof(us_pkt));
    }

    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        status = mxr_thread_join(threads[ix], NULL);
        EXPECT_EQ(status, NO_ERROR, "failed to wait");
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

    mx_handle_t ioport = mx_io_port_create(0u);
    EXPECT_GT(ioport, 0, "could not create io port");

    mx_handle_t event = mx_event_create(0u);
    EXPECT_GT(event, 0, "could not create event");

    mx_handle_t other = mx_io_port_create(0u);
    EXPECT_GT(other, 0, "could not create io port");

    status = mx_io_port_bind(ioport, -1, other, MX_SIGNAL_SIGNALED);
    EXPECT_EQ(status, ERR_INVALID_ARGS, "non waitable objects not allowed");

    status = mx_io_port_bind(ioport, -1, event, MX_SIGNAL_SIGNALED);
    EXPECT_EQ(status, NO_ERROR, "failed to bind event");

    status = mx_io_port_bind(ioport, -1, event, 0u);
    EXPECT_EQ(status, NO_ERROR, "failed to unbind event");

    status = mx_handle_close(ioport);
    EXPECT_EQ(status, NO_ERROR, "failed to close io port");

    status = mx_handle_close(other);
    EXPECT_EQ(status, NO_ERROR, "failed to close io port");

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
    uintptr_t key;
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
        status = mx_io_port_wait(info->io_port, &io_pkt, sizeof(io_pkt));
        if (status != NO_ERROR) {
            info->error = status;
            break;
        }
        if (io_pkt.key == 0) {
            // Normal exit.
            break;
        }

        report_t report = { io_pkt.key, io_pkt.signals };
        status = mx_message_write(info->reply_pipe, &report, sizeof(report), NULL, 0, 0u);
        if (status != NO_ERROR) {
            info->error = status;
            break;
        }

    };

    return 0;
}

static bool bind_events_test(void)
{
    BEGIN_TEST;
    mx_status_t status;
    io_info_t info = {0};

    info.io_port = mx_io_port_create(0u);
    EXPECT_GT(info.io_port, 0, "could not create ioport");

    mx_handle_t h[2];
    status = mx_message_pipe_create(h, 0);
    EXPECT_EQ(status, 0, "could not create pipes");

    mx_handle_t pipe = h[0];
    info.reply_pipe = h[1];

    mx_handle_t events[5];
    for (int ix = 0; ix != countof(events); ++ix) {
        events[ix] = mx_event_create(0u);
        EXPECT_GT(events[ix], 0, "failed to create event");
        status = mx_io_port_bind(info.io_port, -events[ix], events[ix], MX_SIGNAL_SIGNALED);
        EXPECT_EQ(status, NO_ERROR, "failed to bind event to ioport");
    }

    mxr_thread_t *thread;
    status = mxr_thread_create(io_reply_thread, &info, "reply", &thread);
    EXPECT_EQ(status, 0, "could not create thread");

    // Poke at the events in some order, mesages with the events should arrive in order.
    int order[] = {2, 1, 0, 4, 3, 1, 2};
    for (int ix = 0; ix != countof(order); ++ix) {
        status = mx_event_signal(events[order[ix]]);
        EXPECT_EQ(status, NO_ERROR, "could not signal");
        mx_event_reset(events[order[ix]]);
    }

    // Queue a final packet to make io_reply_thread exit.
    mx_user_packet_t us_pkt = {0, {255, 255, 255}};
    status = mx_io_port_queue(info.io_port, &us_pkt, sizeof(us_pkt));

    report_t report;
    uint32_t bytes = sizeof(report);

    // The messages should match the event poke order.
    for (int ix = 0; ix != countof(order); ++ix) {
        status = mx_handle_wait_one(pipe, MX_SIGNAL_READABLE, 1000000000ULL, NULL, NULL);
        EXPECT_EQ(status, NO_ERROR, "failed to wait for pipe");
        status = mx_message_read(pipe, &report, &bytes, NULL, NULL, 0u);
        EXPECT_EQ(status, NO_ERROR, "expected valid message");
        EXPECT_EQ(report.signals, MX_SIGNAL_SIGNALED, "invalid signal");
    }

    status = mxr_thread_join(thread, NULL);
    EXPECT_EQ(status, NO_ERROR, "could not wait for thread");

    // Test cleanup.
    for (int ix = 0; ix != countof(events); ++ix) {
        status = mx_handle_close(events[ix]);
        EXPECT_EQ(status, NO_ERROR, "failed closing events");
    }

    status = mx_handle_close(info.io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");
    status = mx_handle_close(info.reply_pipe);
    EXPECT_EQ(status, NO_ERROR, "failed to close pipe 0");
    status = mx_handle_close(pipe);
    EXPECT_EQ(status, NO_ERROR, "failed to close pipe 1");

    END_TEST;
}

BEGIN_TEST_CASE(io_port_tests)
RUN_TEST(basic_test)
RUN_TEST(thread_pool_test)
RUN_TEST(bind_basic_test)
RUN_TEST(bind_events_test)
END_TEST_CASE(io_port_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
