// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

static void reset_state(mx_fifo_state_t* state) {
    state->head = 0xbad;
    state->tail = 0xbad;
}

static bool check_signals(mx_handle_t fifo, mx_signals_t expected) {
    mx_signals_t pending;
    mx_status_t status = mx_handle_wait_one(fifo, expected, 0u, &pending);
    ASSERT_EQ(status, ERR_TIMED_OUT, "wait failed");
    ASSERT_EQ(pending, expected, "Error with fifo signals");
    return true;
}

static bool basic_test(void) {
    BEGIN_TEST;
    mx_handle_t fifo;
    ASSERT_EQ(mx_fifo_create(0, &fifo), ERR_INVALID_ARGS, "Error during fifo create");
    ASSERT_EQ(mx_fifo_create(3, &fifo), ERR_INVALID_ARGS, "Error during fifo create");
    ASSERT_EQ(mx_fifo_create(4, &fifo), 0, "Error during fifo create");

    mx_fifo_state_t state;
    reset_state(&state);
    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_READ_STATE, 0, &state), 0, "Error getting fifo state");
    ASSERT_EQ(state.head, 0u, "Bad fifo state");
    ASSERT_EQ(state.tail, 0u, "Bad fifo state");
    check_signals(fifo, MX_SIGNAL_FIFO_EMPTY | MX_SIGNAL_FIFO_NOT_FULL);

    reset_state(&state);
    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_ADVANCE_HEAD, 1, &state), 0, "Error advancing head");
    ASSERT_EQ(state.head, 1u, "Error advancing head");
    ASSERT_EQ(state.tail, 0u, "Error advancing tail");
    check_signals(fifo, MX_SIGNAL_FIFO_NOT_EMPTY | MX_SIGNAL_FIFO_NOT_FULL);

    reset_state(&state);
    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_ADVANCE_HEAD, 3, &state), 0, "Error advancing head");
    ASSERT_EQ(state.head, 4u, "Error advancing head");
    ASSERT_EQ(state.tail, 0u, "Error advancing head");
    check_signals(fifo, MX_SIGNAL_FIFO_NOT_EMPTY | MX_SIGNAL_FIFO_FULL);

    ASSERT_GE(mx_handle_close(fifo), 0, "Error closing fifo");
    END_TEST;
}

static bool advance_too_many_test(void) {
    BEGIN_TEST;
    mx_handle_t fifo;
    ASSERT_EQ(mx_fifo_create(4, &fifo), 0, "Error during fifo create");

    mx_fifo_state_t state;
    reset_state(&state);

    // Can't advance head beyond end, or tail past head.
    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_ADVANCE_HEAD, 5, &state), ERR_OUT_OF_RANGE,
            "Error advancing head");
    ASSERT_EQ(state.head, 0u, "Error advancing head");
    ASSERT_EQ(state.tail, 0u, "Error advancing head");

    reset_state(&state);
    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_ADVANCE_TAIL, 1, &state), ERR_OUT_OF_RANGE,
            "Error advancing tail");
    ASSERT_EQ(state.head, 0u, "Error advancing tail");
    ASSERT_EQ(state.tail, 0u, "Error advancing tail");

    // Check advancing tail after head != tail
    reset_state(&state);
    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_ADVANCE_HEAD, 2, &state), 0, "Error advancing head");
    ASSERT_EQ(state.head, 2u, "Error advancing head");
    ASSERT_EQ(state.tail, 0u, "Error advancing head");

    reset_state(&state);
    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_ADVANCE_TAIL, 3, &state), ERR_OUT_OF_RANGE,
            "Error advancing tail");
    ASSERT_EQ(state.head, 2u, "Error advancing tail");
    ASSERT_EQ(state.tail, 0u, "Error advancing tail");

    ASSERT_GE(mx_handle_close(fifo), 0, "Error closing fifo");
    END_TEST;
}

static bool restrict_rights_test(void) {
    BEGIN_TEST;

    mx_handle_t producer, consumer;

    {
        mx_handle_t fifo;
        ASSERT_EQ(mx_fifo_create(4, &fifo), 0, "Error during fifo create");

        ASSERT_EQ(mx_handle_duplicate(fifo, MX_FIFO_CONSUMER_RIGHTS, &consumer), 0,
                "Error duplicating handle for consumer");

        ASSERT_EQ(mx_handle_replace(fifo, MX_FIFO_PRODUCER_RIGHTS, &producer), 0,
                "Error replacing handle for producer");
    }

    // consumer can't move head
    ASSERT_EQ(mx_fifo_op(consumer, MX_FIFO_ADVANCE_HEAD, 1u, NULL), ERR_ACCESS_DENIED,
            "Error advancing head (should have been denied)");

    // move head so fifo is not empty
    ASSERT_EQ(mx_fifo_op(producer, MX_FIFO_ADVANCE_HEAD, 1u, NULL), 0, "Error advancing head");

    // producer can't move tail
    ASSERT_EQ(mx_fifo_op(producer, MX_FIFO_ADVANCE_TAIL, 1u, NULL), ERR_ACCESS_DENIED,
            "Error advancing tail (should have been denied)");

    ASSERT_GE(mx_handle_close(producer), 0, "Error closing fifo");
    ASSERT_GE(mx_handle_close(consumer), 0, "Error closing fifo");
    END_TEST;
}

static int thread_consumer(void* arg) {
    mx_handle_t fifo = *(mx_handle_t*)arg;

    mx_fifo_state_t state;
    reset_state(&state);

    // ensure we can read the fifo state
    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_READ_STATE, 0, &state), 0, "Error getting fifo state");

    mx_signals_t pending;
    ASSERT_EQ(mx_handle_wait_one(fifo, MX_SIGNAL_FIFO_NOT_EMPTY, 1000 * 1000 * 1000, &pending),
            0, "Error waiting on the fifo");
    ASSERT_EQ(pending & MX_SIGNAL_FIFO_NOT_EMPTY, MX_SIGNAL_FIFO_NOT_EMPTY,
            "Error with pending signals");

    ASSERT_EQ(mx_fifo_op(fifo, MX_FIFO_ADVANCE_TAIL, 1u, NULL), 0, "Error advancing tail");

    return 0;
}

static bool multithreaded_test(void) {
    BEGIN_TEST;

    mx_handle_t producer, consumer;

    {
        mx_handle_t fifo;
        ASSERT_EQ(mx_fifo_create(4, &fifo), 0, "Error during fifo create");

        ASSERT_EQ(mx_handle_duplicate(fifo, MX_FIFO_CONSUMER_RIGHTS, &consumer), 0,
                "Error duplicating handle for consumer");

        ASSERT_EQ(mx_handle_replace(fifo, MX_FIFO_PRODUCER_RIGHTS, &producer), 0,
                "Error replacing handle for producer");
    }

    thrd_t consume_thr;
    int ret = thrd_create_with_name(&consume_thr, thread_consumer, &consumer, "consumer");
    ASSERT_EQ(ret, thrd_success, "Error during thread creation");

    mx_nanosleep(1000);
    ASSERT_EQ(mx_fifo_op(producer, MX_FIFO_ADVANCE_HEAD, 1u, NULL), 0, "Error advancing head");

    mx_signals_t pending;
    ASSERT_EQ(mx_handle_wait_one(producer, MX_SIGNAL_FIFO_EMPTY, 1000 * 1000 * 1000, &pending),
            0, "Error waiting on the fifo");
    ASSERT_EQ(pending & MX_SIGNAL_FIFO_EMPTY, MX_SIGNAL_FIFO_EMPTY,
            "Error with pending signals");

    ASSERT_EQ(thrd_join(consume_thr, NULL), thrd_success, "Error during join");

    ASSERT_GE(mx_handle_close(producer), 0, "Error closing fifo");
    ASSERT_GE(mx_handle_close(consumer), 0, "Error closing fifo");
    END_TEST;
}

BEGIN_TEST_CASE(fifo_tests)
RUN_TEST(basic_test)
RUN_TEST(advance_too_many_test)
RUN_TEST(restrict_rights_test)
RUN_TEST(multithreaded_test)
END_TEST_CASE(fifo_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
