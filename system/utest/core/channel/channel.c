// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <magenta/compiler.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/channel.h>
#include <magenta/syscalls/object.h>
#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

mx_handle_t _channel[4];

/**
 * Channel tests with wait multiple.
 *
 * Tests signal state persistence and various combinations of states on multiple handles.
 *
 * Test sequence (may not be exact due to concurrency):
 *   1. Create 2 channels and start a reader thread.
 *   2. Reader blocks wait on both channels.
 *   3. Write to both channels and yield.
 *   4. Reader wake up with channel 1 and channel 2 readable.
 *   5. Reader reads from channel 1, and calls wait again.
 *   6. Reader should wake up immediately, with channel 1 not readable and channel 2 readable.
 *   7. Reader blocks on wait.
 *   8. Write to channel 1 and yield.
 *   9. Reader wake up with channel 1 readable and reads from channel 1.
 *  10. Reader blocks on wait.
 *  11. Write to channel 2 and close both channels, then yield.
 *  12. Reader wake up with channel 2 closed and readable.
 *  13. Read from channel 2 and wait.
 *  14. Reader wake up with channel 2 closed, closes both channels and exit.
 */

static int reader_thread(void* arg) {
    const unsigned int index = 2;
    mx_handle_t* channel = &_channel[index];
    __UNUSED mx_status_t status;
    mx_signals_state_t states[2];
    mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
    unsigned int packets[2] = {0, 0};
    bool closed[2] = {false, false};
    do {
        status = mx_handle_wait_many(2, channel, &signals, MX_TIME_INFINITE, NULL, states);
        assert(status == NO_ERROR);
        uint32_t data;
        uint32_t num_bytes = sizeof(uint32_t);
        if (states[0].satisfied & MX_SIGNAL_READABLE) {
            status = mx_channel_read(channel[0], 0u, &data, num_bytes, &num_bytes,
                                     NULL, 0, NULL);
            assert(status == NO_ERROR);
            packets[0] += 1;
        } else if (states[1].satisfied & MX_SIGNAL_READABLE) {
            status = mx_channel_read(channel[1], 0u, &data, num_bytes, &num_bytes,
                                     NULL, 0, NULL);
            assert(status == NO_ERROR);
            packets[1] += 1;
        } else {
            if (states[0].satisfied & MX_SIGNAL_PEER_CLOSED)
                closed[0] = true;
            if (states[1].satisfied & MX_SIGNAL_PEER_CLOSED)
                closed[1] = true;
        }
    } while (!closed[0] || !closed[1]);
    assert(packets[0] == 3);
    assert(packets[1] == 2);
    return 0;
}

static mx_signals_t get_satisfied_signals(mx_handle_t handle) {
    mx_signals_t pending = 0;
    __UNUSED mx_status_t status = mx_handle_wait_one(handle, 0u, 0u, &pending);
    assert(status == ERR_BAD_STATE);  // "Unsatisfiable".
    return pending;
}

static bool channel_test(void) {
    BEGIN_TEST;

    mx_status_t status;

    mx_handle_t h[2];
    status = mx_channel_create(0, &h[0], &h[1]);
    ASSERT_EQ(status, NO_ERROR, "error in channel create");

    ASSERT_EQ(get_satisfied_signals(h[0]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfied_signals(h[1]), MX_SIGNAL_WRITABLE, "");

    _channel[0] = h[0];
    _channel[2] = h[1];

    static const uint32_t write_data = 0xdeadbeef;
    status = mx_channel_write(_channel[0], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");
    ASSERT_EQ(get_satisfied_signals(_channel[0]), MX_SIGNAL_WRITABLE, "");
    ASSERT_EQ(get_satisfied_signals(_channel[2]), MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE, "");

    status = mx_channel_create(0, &h[0], &h[1]);
    ASSERT_EQ(status, NO_ERROR, "error in channel create");

    _channel[1] = h[0];
    _channel[3] = h[1];

    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, reader_thread, NULL), thrd_success, "error in thread create");

    status = mx_channel_write(_channel[1], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    usleep(1);

    status = mx_channel_write(_channel[0], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    status = mx_channel_write(_channel[0], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    usleep(1);

    status = mx_channel_write(_channel[1], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");

    mx_handle_close(_channel[1]);
    // The reader thread is reading from _channel[3], so we may or may not have "readable".
    ASSERT_TRUE((get_satisfied_signals(_channel[3]) & MX_SIGNAL_PEER_CLOSED), "");

    usleep(1);
    mx_handle_close(_channel[0]);

    EXPECT_EQ(thrd_join(thread, NULL), thrd_success, "error in thread join");

    // Since the the other side of _channel[3] is closed, and the read thread read everything from it,
    // the only satisfied/satisfiable signals should be "peer closed".
    ASSERT_EQ(get_satisfied_signals(_channel[3]), MX_SIGNAL_PEER_CLOSED, "");

    mx_handle_close(_channel[2]);
    mx_handle_close(_channel[3]);

    END_TEST;
}

static bool channel_read_error_test(void) {
    BEGIN_TEST;
    mx_handle_t channel[2];
    mx_status_t status = mx_channel_create(0, &channel[0], &channel[1]);
    ASSERT_EQ(status, NO_ERROR, "error in channel create");

    // Read from an empty channel.
    status = mx_channel_read(channel[0], 0u, NULL, 0, NULL, NULL, 0, NULL);
    ASSERT_EQ(status, ERR_SHOULD_WAIT, "read on empty non-closed channel produced incorrect error");

    char data = 'x';
    status = mx_channel_write(channel[1], 0u, &data, 1u, NULL, 0u);
    ASSERT_EQ(status, NO_ERROR, "write failed");

    mx_handle_close(channel[1]);

    // Read a message with the peer closed, should yield the message.
    char read_data = '\0';
    uint32_t read_data_size = 1u;
    status = mx_channel_read(channel[0], 0u, &read_data, read_data_size, &read_data_size,
                             NULL, 0, NULL);
    ASSERT_EQ(status, NO_ERROR, "read failed with peer closed but message in the channel");
    ASSERT_EQ(read_data_size, 1u, "read returned incorrect number of bytes");
    ASSERT_EQ(read_data, 'x', "read returned incorrect data");

    // Read from an empty channel with a closed peer, should yield a channel closed error.
    status = mx_channel_read(channel[0], 0u, NULL, 0, NULL, NULL, 0, NULL);
    ASSERT_EQ(status, ERR_REMOTE_CLOSED, "read on empty closed channel produced incorrect error");

    // Waiting for readability should yield a bad state error.
    status = mx_handle_wait_one(channel[0], MX_SIGNAL_READABLE, 0u, NULL);
    ASSERT_EQ(status, ERR_BAD_STATE, "waiting for readability should not succeed");

    END_TEST;
}

static bool channel_close_test(void) {
    BEGIN_TEST;
    mx_handle_t channel[2];
    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");
    mx_handle_t channel1[2];
    ASSERT_EQ(mx_channel_create(0, &channel1[0], &channel1[1]), NO_ERROR, "");
    mx_handle_t channel2[2];
    ASSERT_EQ(mx_channel_create(0, &channel2[0], &channel2[1]), NO_ERROR, "");

    // Write channel1[0] to channel[0] (to be received by channel[1]) and channel2[0] to channel[1] (to be received
    // by channel[0]).
    ASSERT_EQ(mx_channel_write(channel[0], 0u, NULL, 0u, &channel1[0], 1u), NO_ERROR, "");
    channel1[0] = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_channel_write(channel[1], 0u, NULL, 0u, &channel2[0], 1u), NO_ERROR, "");
    channel2[0] = MX_HANDLE_INVALID;

    // Close channel[1]; the former channel1[0] should be closed, so channel1[1] should have peer closed.
    ASSERT_EQ(mx_handle_close(channel[1]), NO_ERROR, "");
    channel[1] = MX_HANDLE_INVALID;
    ASSERT_EQ(get_satisfied_signals(channel1[1]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfied_signals(channel2[1]), MX_SIGNAL_WRITABLE, "");

    // Close channel[0]; the former channel2[0] should be closed, so channel2[1] should have peer closed.
    ASSERT_EQ(mx_handle_close(channel[0]), NO_ERROR, "");
    channel[0] = MX_HANDLE_INVALID;
    ASSERT_EQ(get_satisfied_signals(channel1[1]), MX_SIGNAL_PEER_CLOSED, "");
    ASSERT_EQ(get_satisfied_signals(channel2[1]), MX_SIGNAL_PEER_CLOSED, "");

    ASSERT_EQ(mx_handle_close(channel1[1]), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(channel2[1]), NO_ERROR, "");

    END_TEST;
}

static bool channel_non_transferable(void) {
    BEGIN_TEST;

    mx_handle_t channel[2];
    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");
    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), 0, "failed to create event");
    mx_info_handle_basic_t event_handle_info;
    mx_ssize_t get_info_result = mx_object_get_info(event, MX_INFO_HANDLE_BASIC,
            sizeof(event_handle_info.rec), &event_handle_info, sizeof(event_handle_info));
    ASSERT_EQ(get_info_result, (mx_ssize_t)sizeof(event_handle_info), "failed to get event info");
    mx_rights_t initial_event_rights = event_handle_info.rec.rights;
    mx_handle_t non_transferable_event =
            mx_handle_duplicate(event, initial_event_rights & ~MX_RIGHT_TRANSFER);

    mx_status_t write_result = mx_channel_write(channel[0], 0u, NULL, 0, &non_transferable_event, 1u);
    EXPECT_EQ(write_result, ERR_ACCESS_DENIED, "message_write should fail with ACCESS_DENIED");

    mx_status_t close_result = mx_handle_close(non_transferable_event);
    EXPECT_EQ(close_result, NO_ERROR, "");

    END_TEST;
}

static bool channel_duplicate_handles(void) {
    BEGIN_TEST;

    mx_handle_t channel[2];
    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), 0, "failed to create event");

    mx_handle_t dup_handles[2] = { event, event };
    mx_status_t write_result = mx_channel_write(channel[0], 0u, NULL, 0, dup_handles, 2u);
    EXPECT_EQ(write_result, ERR_INVALID_ARGS, "message_write should fail with ERR_INVALID_ARGS");

    mx_status_t close_result = mx_handle_close(event);
    EXPECT_EQ(close_result, NO_ERROR, "");
    close_result = mx_handle_close(channel[0]);
    EXPECT_EQ(close_result, NO_ERROR, "");
    close_result = mx_handle_close(channel[1]);
    EXPECT_EQ(close_result, NO_ERROR, "");

    END_TEST;
}

static const uint32_t multithread_read_num_messages = 5000u;

#define MSG_UNSET       ((uint32_t)-1)
#define MSG_READ_FAILED ((uint32_t)-2)
#define MSG_WRONG_SIZE  ((uint32_t)-3)
#define MSG_BAD_DATA    ((uint32_t)-4)

static int multithread_reader(void* arg) {
    for (uint32_t i = 0; i < multithread_read_num_messages / 2; i++) {
        uint32_t msg = MSG_UNSET;
        uint32_t msg_size = sizeof(msg);
        mx_status_t status = mx_channel_read(_channel[0], 0u, &msg, msg_size, &msg_size,
                                             NULL, 0, NULL);
        if (status != NO_ERROR) {
            ((uint32_t*)arg)[i] = MSG_READ_FAILED;
            break;
        }
        if (msg_size != sizeof(msg)) {
            ((uint32_t*)arg)[i] = MSG_WRONG_SIZE;
            break;
        }
        if (msg >= multithread_read_num_messages) {
            ((uint32_t*)arg)[i] = MSG_BAD_DATA;
            break;
        }

        ((uint32_t*)arg)[i] = msg;
    }
    return 0;
}

static bool channel_multithread_read(void) {
    BEGIN_TEST;

    // We'll write from channel[0] and read from channel[1].
    mx_handle_t channel[2];
    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");

    for (uint32_t i = 0; i < multithread_read_num_messages; i++)
        ASSERT_EQ(mx_channel_write(channel[0], 0, &i, sizeof(i), NULL, 0), NO_ERROR, "");

    _channel[0] = channel[1];

    // Start two threads to read messages (each will read half). Each will store the received
    // message data in the corresponding array.
    uint32_t* received0 = malloc(multithread_read_num_messages / 2 * sizeof(uint32_t));
    ASSERT_TRUE(received0, "malloc failed");
    uint32_t* received1 = malloc(multithread_read_num_messages / 2 * sizeof(uint32_t));
    ASSERT_TRUE(received1, "malloc failed");
    thrd_t reader0;
    ASSERT_EQ(thrd_create(&reader0, multithread_reader, received0), thrd_success,
              "thrd_create failed");
    thrd_t reader1;
    ASSERT_EQ(thrd_create(&reader1, multithread_reader, received1), thrd_success,
              "thrd_create failed");

    // Wait for threads.
    EXPECT_EQ(thrd_join(reader0, NULL), thrd_success, "");
    EXPECT_EQ(thrd_join(reader1, NULL), thrd_success, "");

    EXPECT_EQ(mx_handle_close(channel[0]), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(channel[1]), NO_ERROR, "");

    // Check data.
    bool* received_flags = calloc(multithread_read_num_messages, sizeof(bool));

    for (uint32_t i = 0; i < multithread_read_num_messages / 2; i++) {
        uint32_t msg = received0[i];
        ASSERT_NEQ(msg, MSG_READ_FAILED, "read failed");
        ASSERT_NEQ(msg, MSG_WRONG_SIZE, "got wrong message size");
        ASSERT_NEQ(msg, MSG_BAD_DATA, "got bad message data");
        ASSERT_LT(msg, multithread_read_num_messages, "???");
        ASSERT_FALSE(received_flags[msg], "got duplicate message");
    }
    for (uint32_t i = 0; i < multithread_read_num_messages / 2; i++) {
        uint32_t msg = received1[i];
        ASSERT_NEQ(msg, MSG_READ_FAILED, "read failed");
        ASSERT_NEQ(msg, MSG_WRONG_SIZE, "got wrong message size");
        ASSERT_NEQ(msg, MSG_BAD_DATA, "got bad message data");
        ASSERT_LT(msg, multithread_read_num_messages, "???");
        ASSERT_FALSE(received_flags[msg], "got duplicate message");
    }

    free(received0);
    free(received1);
    free(received_flags);

    _channel[0] = MX_HANDLE_INVALID;

    END_TEST;
}

// |handle| must be valid (and duplicatable and transferable) if |num_handles > 0|.
static void write_test_message(mx_handle_t channel,
                               mx_handle_t handle,
                               uint32_t size,
                               uint32_t num_handles) {
    static const char data[1000] = {};
    mx_handle_t handles[10] = {};

    assert(size <= sizeof(data));
    assert(num_handles <= countof(handles));

    for (uint32_t i = 0; i < num_handles; i++) {
        handles[i] = mx_handle_duplicate(handle, MX_RIGHT_TRANSFER);
        assert(handles[i] != MX_HANDLE_INVALID);
    }

    __UNUSED mx_status_t status = mx_channel_write(channel, 0u, data, size, handles, num_handles);
    assert(status == NO_ERROR);
}

static bool channel_may_discard(void) {
    BEGIN_TEST;

    mx_handle_t channel[2];
    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), 0, "failed to create event");

    EXPECT_EQ(mx_handle_wait_one(channel[1], MX_SIGNAL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    write_test_message(channel[0], event, 10u, 0u);
    EXPECT_EQ(mx_channel_read(channel[1], MX_CHANNEL_READ_MAY_DISCARD, NULL, 0, NULL, NULL, 0, NULL),
              ERR_BUFFER_TOO_SMALL, "");

    EXPECT_EQ(mx_handle_wait_one(channel[1], MX_SIGNAL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    char data[1000];
    uint32_t size;

    write_test_message(channel[0], event, 100u, 0u);
    size = 10u;
    EXPECT_EQ(mx_channel_read(channel[1], MX_CHANNEL_READ_MAY_DISCARD, data, size, &size, NULL, 0, NULL),
              ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 100u, "wrong size");

    EXPECT_EQ(mx_handle_wait_one(channel[1], MX_SIGNAL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    mx_handle_t handles[10];
    uint32_t num_handles;

    write_test_message(channel[0], event, 0u, 5u);
    size = 10u;
    num_handles = 1u;
    EXPECT_EQ(mx_channel_read(channel[1], MX_CHANNEL_READ_MAY_DISCARD, data, size, &size,
                              handles, num_handles, &num_handles),
              ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 0u, "wrong size");
    EXPECT_EQ(num_handles, 5u, "wrong number of handles");

    EXPECT_EQ(mx_handle_wait_one(channel[1], MX_SIGNAL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    write_test_message(channel[0], event, 100u, 5u);
    size = 10u;
    num_handles = 1u;
    EXPECT_EQ(mx_channel_read(channel[1], MX_CHANNEL_READ_MAY_DISCARD, data, size, &size,
                              handles, num_handles, &num_handles),
              ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 100u, "wrong size");
    EXPECT_EQ(num_handles, 5u, "wrong number of handles");

    EXPECT_EQ(mx_handle_wait_one(channel[1], MX_SIGNAL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    mx_status_t close_result = mx_handle_close(event);
    EXPECT_EQ(close_result, NO_ERROR, "");
    close_result = mx_handle_close(channel[0]);
    EXPECT_EQ(close_result, NO_ERROR, "");
    close_result = mx_handle_close(channel[1]);
    EXPECT_EQ(close_result, NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(channel_tests)
RUN_TEST(channel_test)
RUN_TEST(channel_read_error_test)
RUN_TEST(channel_close_test)
RUN_TEST(channel_non_transferable)
RUN_TEST(channel_duplicate_handles)
RUN_TEST(channel_multithread_read)
RUN_TEST(channel_may_discard)
END_TEST_CASE(channel_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
