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

    mx_port_uq_event_t uq_event;
    mx_status_t status;
    intptr_t key;

    while (true) {
        status = _magenta_io_port_wait(tinfo->io_port, &key, &uq_event, sizeof(uq_event));

        if (status < 0) {
            tinfo->error = status;
            break;
        } else if (key < 0) {
            tinfo->error = -1;
            break;
        } else if (key >= NUM_SLOTS) {
            // expected termination.
            break;
        }

        tinfo->work_count[(int)key] += uq_event.param[0];
        _magenta_nanosleep(1u);
    };

    _magenta_thread_exit();
    return 0;
}

static bool basic_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    mx_handle_t io_port = _magenta_io_port_create(0u);
    EXPECT_GT(io_port, 0, "could not create ioport");

    mx_port_uq_event_t uq_event = {0};

    status = _magenta_io_port_queue(io_port, 1, &uq_event, 8u);
    EXPECT_EQ(status, ERR_INVALID_ARGS, "expected failure");

    status = _magenta_io_port_queue(io_port, -1, &uq_event, sizeof(uq_event));
    EXPECT_EQ(status, ERR_INVALID_ARGS, "expected failure");

    intptr_t key = 0;

    status = _magenta_io_port_wait(io_port, &key, &uq_event, 8u);
    EXPECT_EQ(status, ERR_INVALID_ARGS, "expected failure");

    int slots = 0;

    while (true) {
        status = _magenta_io_port_queue(io_port, (128 - slots), &uq_event, sizeof(uq_event));
        if (status == ERR_NOT_ENOUGH_BUFFER)
            break;
        EXPECT_EQ(status, NO_ERROR, "could not queue");
        ++slots;
    }

    EXPECT_EQ(slots, 128, "incorrect number of slots");

    status = _magenta_io_port_wait(io_port, &key, &uq_event, sizeof(uq_event));
    EXPECT_EQ(status, NO_ERROR, "failed to dequeue");
    EXPECT_EQ(key, 128, "wrong key");

    status = _magenta_handle_close(io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");

    END_TEST;
}

static bool thread_pool_test(void)
{
    BEGIN_TEST;
    mx_status_t status;

    t_info_t tinfo = {0u, 0, {0}};

    tinfo.io_port = _magenta_io_port_create(0u);
    EXPECT_GT(tinfo.io_port, 0, "could not create ioport");

    mx_handle_t threads[NUM_IO_THREADS];
    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        threads[ix] = _magenta_thread_create(thread_consumer, &tinfo, "tpool", 5);
        EXPECT_GT(threads[ix], 0, "could not create thread");
    }

    mx_port_uq_event_t uq_event = {0};

    for (size_t ix = 0; ix != NUM_SLOTS + NUM_IO_THREADS; ++ix) {
        uq_event.param[0] = 10 + ix;
        _magenta_io_port_queue(tinfo.io_port, ix, &uq_event, sizeof(uq_event));
    }

    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        status = _magenta_handle_wait_one(
            threads[ix], MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
        EXPECT_EQ(status, NO_ERROR, "failed to wait");
    }

    status = _magenta_handle_close(tinfo.io_port);
    EXPECT_EQ(status, NO_ERROR, "failed to close ioport");

    int sum = 0;
    for (size_t ix = 0; ix != NUM_SLOTS; ++ix) {
        int slot = tinfo.work_count[ix];
        EXPECT_GT(slot, 0, "bad slot entry");
        sum += slot;
    }
    EXPECT_EQ(sum, 145, "bad sum");

    for (size_t ix = 0; ix != NUM_IO_THREADS; ++ix) {
        status = _magenta_handle_close(threads[ix]);
        EXPECT_EQ(status, NO_ERROR, "failed to close thread handle");
    }

    END_TEST;
}

BEGIN_TEST_CASE(io_port_tests)
RUN_TEST(basic_test)
RUN_TEST(thread_pool_test)
END_TEST_CASE(io_port_tests)

int main(void) {
    return unittest_run_all_tests() ? 0 : -1;
}
