// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <zircon/compiler.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

static zx_handle_t _channel[4];

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
    zx_handle_t* channel = &_channel[index];
    __UNUSED zx_status_t status;
    unsigned int packets[2] = {0, 0};
    bool closed[2] = {false, false};
    zx_wait_item_t items[2];
    items[0].handle = channel[0];
    items[1].handle = channel[1];
    items[0].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    items[1].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    do {
        status = zx_object_wait_many(items, 2, ZX_TIME_INFINITE);
        assert(status == ZX_OK);
        uint32_t data;
        uint32_t num_bytes = sizeof(uint32_t);
        if (items[0].pending & ZX_CHANNEL_READABLE) {
            status = zx_channel_read(channel[0], 0u, &data, NULL,
                                     num_bytes, 0, &num_bytes, NULL);
            assert(status == ZX_OK);
            packets[0] += 1;
        } else if (items[1].pending & ZX_CHANNEL_READABLE) {
            status = zx_channel_read(channel[1], 0u, &data, NULL,
                                     num_bytes, 0, &num_bytes, NULL);
            assert(status == ZX_OK);
            packets[1] += 1;
        } else {
            if (items[0].pending & ZX_CHANNEL_PEER_CLOSED)
                closed[0] = true;
            if (items[1].pending & ZX_CHANNEL_PEER_CLOSED)
                closed[1] = true;
        }
    } while (!closed[0] || !closed[1]);
    assert(packets[0] == 3);
    assert(packets[1] == 2);
    return 0;
}

static zx_signals_t get_satisfied_signals(zx_handle_t handle) {
    zx_signals_t pending = 0;
    __UNUSED zx_status_t status = zx_object_wait_one(handle, 0u, 0u, &pending);
    assert(status == ZX_ERR_TIMED_OUT);
    return pending;
}

static bool channel_test(void) {
    BEGIN_TEST;

    zx_status_t status;

    zx_handle_t h[2];
    status = zx_channel_create(0, &h[0], &h[1]);
    ASSERT_EQ(status, ZX_OK, "error in channel create");

    // Check that koids line up.
    zx_info_handle_basic_t info[2] = {};
    status = zx_object_get_info(h[0], ZX_INFO_HANDLE_BASIC, &info[0], sizeof(info[0]), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "");
    status = zx_object_get_info(h[1], ZX_INFO_HANDLE_BASIC, &info[1], sizeof(info[1]), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_NE(info[0].koid, 0u, "zero koid!");
    ASSERT_NE(info[0].related_koid, 0u, "zero peer koid!");
    ASSERT_NE(info[1].koid, 0u, "zero koid!");
    ASSERT_NE(info[1].related_koid, 0u, "zero peer koid!");
    ASSERT_EQ(info[0].koid, info[1].related_koid, "mismatched koids!");
    ASSERT_EQ(info[1].koid, info[0].related_koid, "mismatched koids!");

    ASSERT_EQ(get_satisfied_signals(h[0]), ZX_CHANNEL_WRITABLE, "");
    ASSERT_EQ(get_satisfied_signals(h[1]), ZX_CHANNEL_WRITABLE, "");

    _channel[0] = h[0];
    _channel[2] = h[1];

    static const uint32_t write_data = 0xdeadbeef;
    status = zx_channel_write(_channel[0], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, ZX_OK, "error in message write");
    ASSERT_EQ(get_satisfied_signals(
        _channel[0]), ZX_CHANNEL_WRITABLE, "");
    ASSERT_EQ(get_satisfied_signals(
        _channel[2]), ZX_CHANNEL_READABLE | ZX_CHANNEL_WRITABLE, "");

    status = zx_channel_create(0, &h[0], &h[1]);
    ASSERT_EQ(status, ZX_OK, "error in channel create");

    _channel[1] = h[0];
    _channel[3] = h[1];

    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, reader_thread, NULL), thrd_success, "error in thread create");

    status = zx_channel_write(_channel[1], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, ZX_OK, "error in message write");

    usleep(1);

    status = zx_channel_write(_channel[0], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, ZX_OK, "error in message write");

    status = zx_channel_write(_channel[0], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, ZX_OK, "error in message write");

    usleep(1);

    status = zx_channel_write(_channel[1], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, ZX_OK, "error in message write");

    zx_handle_close(_channel[1]);
    // The reader thread is reading from _channel[3], so we may or may not have "readable".
    ASSERT_TRUE((get_satisfied_signals(_channel[3]) & ZX_CHANNEL_PEER_CLOSED), "");

    usleep(1);
    zx_handle_close(_channel[0]);

    EXPECT_EQ(thrd_join(thread, NULL), thrd_success, "error in thread join");

    // Since the the other side of _channel[3] is closed, and the read thread read everything
    // from it, the only satisfied/satisfiable signals should be "peer closed".
    ASSERT_EQ(get_satisfied_signals(
        _channel[3]), ZX_CHANNEL_PEER_CLOSED, "");

    zx_handle_close(_channel[2]);
    zx_handle_close(_channel[3]);

    END_TEST;
}

static bool channel_read_error_test(void) {
    BEGIN_TEST;
    zx_handle_t channel[2];
    zx_status_t status = zx_channel_create(0, &channel[0], &channel[1]);
    ASSERT_EQ(status, ZX_OK, "error in channel create");

    // Read from an empty channel.
    status = zx_channel_read(channel[0], 0u, NULL, NULL, 0, 0, NULL, NULL);
    ASSERT_EQ(status, ZX_ERR_SHOULD_WAIT, "read on empty non-closed channel produced incorrect error");

    char data = 'x';
    status = zx_channel_write(channel[1], 0u, &data, 1u, NULL, 0u);
    ASSERT_EQ(status, ZX_OK, "write failed");

    zx_handle_close(channel[1]);

    // Read a message with the peer closed, should yield the message.
    char read_data = '\0';
    uint32_t read_data_size = 1u;
    status = zx_channel_read(channel[0], 0u, &read_data, NULL,
                             read_data_size, 0, &read_data_size, NULL);
    ASSERT_EQ(status, ZX_OK, "read failed with peer closed but message in the channel");
    ASSERT_EQ(read_data_size, 1u, "read returned incorrect number of bytes");
    ASSERT_EQ(read_data, 'x', "read returned incorrect data");

    // Read from an empty channel with a closed peer, should yield a channel closed error.
    status = zx_channel_read(channel[0], 0u, NULL, NULL, 0, 0, NULL, NULL);
    ASSERT_EQ(status, ZX_ERR_PEER_CLOSED, "read on empty closed channel produced incorrect error");

    END_TEST;
}

static bool channel_close_test(void) {
    BEGIN_TEST;
    zx_handle_t channel[2];

    // Channels should gain PEER_CLOSED (and lose WRITABLE) if their peer is closed
    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(channel[1]), ZX_OK, "");
    ASSERT_EQ(get_satisfied_signals(
        channel[0]), ZX_CHANNEL_PEER_CLOSED, "");
    ASSERT_EQ(zx_handle_close(channel[0]), ZX_OK, "");

    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");
    zx_handle_t channel1[2];
    ASSERT_EQ(zx_channel_create(0, &channel1[0], &channel1[1]), ZX_OK, "");
    zx_handle_t channel2[2];
    ASSERT_EQ(zx_channel_create(0, &channel2[0], &channel2[1]), ZX_OK, "");

    // Write channel1[0] to channel[0] (to be received by channel[1])
    // and channel2[0] to channel[1] (to be received by channel[0]).
    ASSERT_EQ(zx_channel_write(channel[0], 0u, NULL, 0u, &channel1[0], 1u), ZX_OK, "");
    channel1[0] = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_channel_write(channel[1], 0u, NULL, 0u, &channel2[0], 1u), ZX_OK, "");
    channel2[0] = ZX_HANDLE_INVALID;

    // Close channel[1]; the former channel1[0] should be closed, so channel1[1] should have
    // peer closed.
    ASSERT_EQ(zx_handle_close(channel[1]), ZX_OK, "");
    channel[1] = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_object_wait_one(
        channel1[1], ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, NULL), ZX_OK, "");
    ASSERT_EQ(get_satisfied_signals(
        channel2[1]), ZX_CHANNEL_WRITABLE, "");

    // Close channel[0]; the former channel2[0] should be closed, so channel2[1]
    // should have peer closed.
    ASSERT_EQ(zx_handle_close(channel[0]), ZX_OK, "");
    channel[0] = ZX_HANDLE_INVALID;
    ASSERT_EQ(get_satisfied_signals(
        channel1[1]), ZX_CHANNEL_PEER_CLOSED, "");
    ASSERT_EQ(zx_object_wait_one(
        channel2[1], ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, NULL), ZX_OK, "");

    ASSERT_EQ(zx_handle_close(channel1[1]), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(channel2[1]), ZX_OK, "");

    END_TEST;
}

static bool channel_non_transferable(void) {
    BEGIN_TEST;

    zx_handle_t channel[2];
    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");
    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), 0, "failed to create event");
    zx_info_handle_basic_t event_handle_info;

    zx_status_t status = zx_object_get_info(event, ZX_INFO_HANDLE_BASIC, &event_handle_info,
                                            sizeof(event_handle_info), NULL, NULL);
    ASSERT_EQ(status, ZX_OK, "failed to get event info");
    zx_rights_t initial_event_rights = event_handle_info.rights;
    zx_handle_t non_transferable_event;
    zx_handle_duplicate(
        event, initial_event_rights & ~ZX_RIGHT_TRANSFER, &non_transferable_event);

    zx_status_t write_result = zx_channel_write(
        channel[0], 0u, NULL, 0, &non_transferable_event, 1u);
    EXPECT_EQ(write_result, ZX_ERR_ACCESS_DENIED, "message_write should fail with ACCESS_DENIED");

    zx_status_t close_result = zx_handle_close(non_transferable_event);
    EXPECT_EQ(close_result, ZX_OK, "");

    END_TEST;
}

static bool channel_duplicate_handles(void) {
    BEGIN_TEST;

    zx_handle_t channel[2];
    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), 0, "failed to create event");

    zx_handle_t dup_handles[2] = { event, event };
    zx_status_t write_result = zx_channel_write(channel[0], 0u, NULL, 0, dup_handles, 2u);
    EXPECT_EQ(write_result, ZX_ERR_INVALID_ARGS, "message_write should fail with ZX_ERR_INVALID_ARGS");

    zx_status_t close_result = zx_handle_close(event);
    EXPECT_EQ(close_result, ZX_OK, "");
    close_result = zx_handle_close(channel[0]);
    EXPECT_EQ(close_result, ZX_OK, "");
    close_result = zx_handle_close(channel[1]);
    EXPECT_EQ(close_result, ZX_OK, "");

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
        zx_status_t status = zx_channel_read(_channel[0], 0u, &msg, NULL,
                                             msg_size, 0, &msg_size, NULL);
        if (status != ZX_OK) {
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
    zx_handle_t channel[2];
    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");

    for (uint32_t i = 0; i < multithread_read_num_messages; i++)
        ASSERT_EQ(zx_channel_write(channel[0], 0, &i, sizeof(i), NULL, 0), ZX_OK, "");

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

    EXPECT_EQ(zx_handle_close(channel[0]), ZX_OK, "");
    EXPECT_EQ(zx_handle_close(channel[1]), ZX_OK, "");

    // Check data.
    bool* received_flags = calloc(multithread_read_num_messages, sizeof(bool));

    for (uint32_t i = 0; i < multithread_read_num_messages / 2; i++) {
        uint32_t msg = received0[i];
        ASSERT_NE(msg, MSG_READ_FAILED, "read failed");
        ASSERT_NE(msg, MSG_WRONG_SIZE, "got wrong message size");
        ASSERT_NE(msg, MSG_BAD_DATA, "got bad message data");
        ASSERT_LT(msg, multithread_read_num_messages, "???");
        ASSERT_FALSE(received_flags[msg], "got duplicate message");
    }
    for (uint32_t i = 0; i < multithread_read_num_messages / 2; i++) {
        uint32_t msg = received1[i];
        ASSERT_NE(msg, MSG_READ_FAILED, "read failed");
        ASSERT_NE(msg, MSG_WRONG_SIZE, "got wrong message size");
        ASSERT_NE(msg, MSG_BAD_DATA, "got bad message data");
        ASSERT_LT(msg, multithread_read_num_messages, "???");
        ASSERT_FALSE(received_flags[msg], "got duplicate message");
    }

    free(received0);
    free(received1);
    free(received_flags);

    _channel[0] = ZX_HANDLE_INVALID;

    END_TEST;
}

// |handle| must be valid (and duplicatable and transferable) if |num_handles > 0|.
static void write_test_message(zx_handle_t channel,
                               zx_handle_t handle,
                               uint32_t size,
                               uint32_t num_handles) {
    static const char data[1000] = {};
    zx_handle_t handles[10] = {};

    assert(size <= sizeof(data));
    assert(num_handles <= countof(handles));

    for (uint32_t i = 0; i < num_handles; i++) {
        zx_status_t status = zx_handle_duplicate(handle, ZX_RIGHT_TRANSFER, &handles[i]);
        assert(status == ZX_OK);
    }

    __UNUSED zx_status_t status = zx_channel_write(channel, 0u, data, size, handles, num_handles);
    assert(status == ZX_OK);
}

static bool channel_may_discard(void) {
    BEGIN_TEST;

    zx_handle_t channel[2];
    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), 0, "failed to create event");

    EXPECT_EQ(zx_object_wait_one(channel[1], ZX_CHANNEL_READABLE, 0u, NULL), ZX_ERR_TIMED_OUT, "");

    write_test_message(channel[0], event, 10u, 0u);
    EXPECT_EQ(zx_channel_read(channel[1], ZX_CHANNEL_READ_MAY_DISCARD, NULL, NULL, 0, 0, NULL, NULL),
              ZX_ERR_BUFFER_TOO_SMALL, "");

    EXPECT_EQ(zx_object_wait_one(channel[1], ZX_CHANNEL_READABLE, 0u, NULL), ZX_ERR_TIMED_OUT, "");

    char data[1000];
    uint32_t size;

    write_test_message(channel[0], event, 100u, 0u);
    size = 10u;
    EXPECT_EQ(zx_channel_read(channel[1], ZX_CHANNEL_READ_MAY_DISCARD, data, NULL, size, 0, &size, NULL),
              ZX_ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 100u, "wrong size");

    EXPECT_EQ(zx_object_wait_one(channel[1], ZX_CHANNEL_READABLE, 0u, NULL), ZX_ERR_TIMED_OUT, "");

    zx_handle_t handles[10];
    uint32_t num_handles;

    write_test_message(channel[0], event, 0u, 5u);
    size = 10u;
    num_handles = 1u;
    EXPECT_EQ(zx_channel_read(channel[1], ZX_CHANNEL_READ_MAY_DISCARD, data, handles,
                              size, num_handles, &size, &num_handles),
              ZX_ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 0u, "wrong size");
    EXPECT_EQ(num_handles, 5u, "wrong number of handles");

    EXPECT_EQ(zx_object_wait_one(channel[1], ZX_CHANNEL_READABLE, 0u, NULL), ZX_ERR_TIMED_OUT, "");

    write_test_message(channel[0], event, 100u, 5u);
    size = 10u;
    num_handles = 1u;
    EXPECT_EQ(zx_channel_read(channel[1], ZX_CHANNEL_READ_MAY_DISCARD, data, handles,
                              size, num_handles, &size, &num_handles),
              ZX_ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 100u, "wrong size");
    EXPECT_EQ(num_handles, 5u, "wrong number of handles");

    EXPECT_EQ(zx_object_wait_one(channel[1], ZX_CHANNEL_READABLE, 0u, NULL), ZX_ERR_TIMED_OUT, "");

    zx_status_t close_result = zx_handle_close(event);
    EXPECT_EQ(close_result, ZX_OK, "");
    close_result = zx_handle_close(channel[0]);
    EXPECT_EQ(close_result, ZX_OK, "");
    close_result = zx_handle_close(channel[1]);
    EXPECT_EQ(close_result, ZX_OK, "");

    END_TEST;
}


static uint32_t call_test_done = 0;
static mtx_t call_test_lock;
static cnd_t call_test_cvar;

// we use txid_t for cmd here so that the test
// works with both 32bit and 64bit txids
typedef struct {
    zx_txid_t txid;
    zx_txid_t cmd;
    uint32_t bit;
    unsigned action;
    zx_status_t expect;
    zx_status_t expect_rs;
    const char* name;
    const char* err;
    int val;
    zx_handle_t h;
    thrd_t t;
} ccargs_t;

#define SRV_SEND_HANDLE   0x0001
#define SRV_SEND_DATA     0x0002
#define SRV_DISCARD       0x0004
#define CLI_SHORT_WAIT    0x0100
#define CLI_RECV_HANDLE   0x0200
#define CLI_SEND_HANDLE   0x0400

#define TEST_SHORT_WAIT_MS    250
#define TEST_LONG_WAIT_MS   10000

static int call_client(void* _args) {
    ccargs_t* ccargs = _args;
    zx_channel_call_args_t args;

    zx_txid_t data[2];
    zx_handle_t txhandle = 0;
    zx_handle_t rxhandle = 0;

    zx_status_t r = ZX_OK;
    if (ccargs->action & CLI_SEND_HANDLE) {
        if ((r = zx_event_create(0, &txhandle)) != ZX_OK) {
            ccargs->err = "failed to create event";
            goto done;
        }
    }

    args.wr_bytes = ccargs;
    args.wr_handles = &txhandle;
    args.wr_num_bytes = sizeof(ccargs_t);
    args.wr_num_handles = (ccargs->action & CLI_SEND_HANDLE) ? 1 : 0;
    args.rd_bytes = data;
    args.rd_handles = &rxhandle;
    args.rd_num_bytes = sizeof(data);
    args.rd_num_handles = (ccargs->action & CLI_RECV_HANDLE) ? 1 : 0;

    uint32_t act_bytes = 0xffffffff;
    uint32_t act_handles = 0xffffffff;

    zx_time_t deadline = (ccargs->action & CLI_SHORT_WAIT) ?
        zx_deadline_after(ZX_MSEC(TEST_SHORT_WAIT_MS)) :
        zx_deadline_after(ZX_MSEC(TEST_LONG_WAIT_MS));

    zx_status_t rs = ZX_OK;
    if ((r = zx_channel_call(ccargs->h, 0, deadline, &args, &act_bytes, &act_handles, &rs)) != ccargs->expect) {
        ccargs->err = "channel call returned";
        ccargs->val = r;
    }
    if (txhandle && (r < 0)) {
        zx_handle_close(txhandle);
    }
    if (rxhandle) {
        zx_handle_close(rxhandle);
    }
    if (r == ZX_ERR_CALL_FAILED) {
        if (ccargs->expect_rs && (ccargs->expect_rs != rs)) {
            ccargs->err = "read_status not what was expected";
            ccargs->val = ccargs->expect_rs;
        } else {
            r = ZX_OK;
        }
    } else if (r == ZX_OK) {
        if (act_bytes != sizeof(data)) {
            ccargs->err = "expected 8 bytes";
            ccargs->val = act_bytes;
        } else if (ccargs->txid != data[0]) {
            ccargs->err = "mismatched txid";
            ccargs->val = data[0];
        } else if (ccargs->cmd != data[1]) {
            ccargs->err = "mismatched cmd";
            ccargs->val = data[1];
        } else if ((ccargs->action & CLI_RECV_HANDLE) && (act_handles != 1)) {
            ccargs->err = "recv handle missing";
        }
    } else if ((r == ZX_ERR_TIMED_OUT) && (ccargs->action & CLI_SHORT_WAIT)) {
        // We expect CLI_SHORT_WAIT calls to time-out.
        r = ZX_OK;
    }

done:
    mtx_lock(&call_test_lock);
    call_test_done |= ccargs->bit;
    cnd_broadcast(&call_test_cvar);
    mtx_unlock(&call_test_lock);
    return (int) r;
}

static ccargs_t ccargs[] = {
    {
        .name = "too large reply",
        .action = SRV_SEND_DATA,
        .expect = ZX_ERR_CALL_FAILED,
        .expect_rs = ZX_ERR_BUFFER_TOO_SMALL,
    },
    {
        .name = "no reply (short wait)",
        .action = SRV_DISCARD | CLI_SHORT_WAIT,
        .expect = ZX_ERR_TIMED_OUT,
    },
    {
        .name = "reply handle",
        .action = SRV_SEND_HANDLE | CLI_RECV_HANDLE,
    },
    {
        .name = "unwanted reply handle",
        .action = SRV_SEND_HANDLE,
        .expect = ZX_ERR_CALL_FAILED,
        .expect_rs = ZX_ERR_BUFFER_TOO_SMALL,
    },
    {
        .name = "send-handle",
        .action = CLI_SEND_HANDLE,
    },
    {
        .name = "send-recv-handle",
        .action = CLI_SEND_HANDLE | CLI_RECV_HANDLE | SRV_SEND_HANDLE,
    },
    {
        .name = "basic",
    },
    {
        .name = "basic",
    },
    {
        .name = "basic",
    },
    {
        .name = "basic",
    },
};

static int call_server(void* ptr) {
    zx_handle_t h = (zx_handle_t) (uintptr_t) ptr;

    ccargs_t msg[countof(ccargs)];
    memset(msg, 0, sizeof(msg));

    zx_status_t status;

    // received the expected number of messages
    for (unsigned n = 0; n < countof(ccargs); n++) {
        zx_object_wait_one(h, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, NULL);

        uint32_t bytes = sizeof(msg[0]);
        uint32_t handles = 1;
        zx_handle_t handle = 0;
        status = zx_channel_read(h, 0, &msg[n], &handle, bytes, handles, &bytes, &handles);
        if (status != ZX_OK) {
            fprintf(stderr, "call_server() read failed\n");
            return (int) status;
        }
        if (handle) {
            status = zx_handle_close(handle);
            if (status != ZX_OK)
                return (int) status;
        }
    }

    // reply to them in reverse order received
    for (unsigned n = 0; n < countof(ccargs); n++) {
        ccargs_t* m = &msg[countof(ccargs) - n - 1];

        if (m->action & SRV_DISCARD) {
            continue;
        }

        zx_txid_t data[4];
        data[0] = m->txid;
        data[1] = m->txid * 31337;
        data[2] = 0x22222222;
        data[3] = 0x33333333;

        uint32_t bytes = sizeof(zx_txid_t) * ((m->action & SRV_SEND_DATA) ? 4 : 2);
        uint32_t handles = (m->action & SRV_SEND_HANDLE) ? 1 : 0;
        zx_handle_t handle = 0;
        if (handles) {
            zx_event_create(0, &handle);
        }
        status = zx_channel_write(h, 0, data, bytes, &handle, handles);
        if (status != ZX_OK) {
            fprintf(stderr, "call_server() write failed\n");
            return (int) status;
        }
    }
    return 0;
}

static bool channel_call(void) {
    BEGIN_TEST;

    ASSERT_EQ(mtx_init(&call_test_lock, mtx_plain), thrd_success, "");
    ASSERT_EQ(cnd_init(&call_test_cvar), thrd_success, "");

    zx_handle_t cli, srv;
    ASSERT_EQ(zx_channel_create(0, &cli, &srv), ZX_OK, "");

    // start test server
    thrd_t srvt;
    ASSERT_EQ(thrd_create(&srvt, call_server, (void*) (uintptr_t) srv), thrd_success, "");

    // start test clients
    uint32_t waitfor = 0;
    for (unsigned n = 0; n < countof(ccargs); n++) {
        ccargs[n].txid = 0x11223300 | n;
        ccargs[n].cmd = ccargs[n].txid * 31337;
        ccargs[n].h = cli;
        ccargs[n].bit = 1 << n;
        waitfor |= ccargs[n].bit;
        ASSERT_EQ(thrd_create(&ccargs[n].t, call_client, &ccargs[n]), thrd_success, "");
    }

    // Wait for all tests to finish. There is no timeout, we leave that to
    // the test harness.
    int r = 0;
    while (r == 0) {
        mtx_lock(&call_test_lock);
        if (call_test_done == waitfor) {
            r = -1;
        } else {
            r = cnd_timedwait(&call_test_cvar, &call_test_lock, NULL);
            EXPECT_EQ(r, thrd_success, "wait failed");
        }
        mtx_unlock(&call_test_lock);
    }

    // report tests that failed or failed to complete
    mtx_lock(&call_test_lock);
    for (unsigned n = 0; n < countof(ccargs); n++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "#%d '%s' did not complete", n, ccargs[n].name);
        EXPECT_EQ(ccargs[n].bit & call_test_done, ccargs[n].bit, buf);
        snprintf(buf, sizeof(buf), "'%s' did not succeed", ccargs[n].name);
        EXPECT_NULL(ccargs[n].err, buf);
        if (ccargs[n].err) {
            unittest_printf_critical("call_client #%d: %s: %s %d (0x%x)\n", n, ccargs[n].name,
                                     ccargs[n].err, ccargs[n].val, ccargs[n].val);
        }
    }
    mtx_unlock(&call_test_lock);

    int retv = 0;
    EXPECT_EQ(thrd_join(srvt, &retv), thrd_success, "");
    EXPECT_EQ(retv, 0, "");

    for (unsigned n = 0; n < countof(ccargs); n++) {
        EXPECT_EQ(thrd_join(ccargs[n].t, &retv), thrd_success, "");
        EXPECT_EQ(retv, 0, "");
    }

    zx_handle_close(cli);
    zx_handle_close(srv);

    END_TEST;
}

static bool create_and_nest(zx_handle_t out, zx_handle_t* end, size_t n) {
    BEGIN_TEST;

    zx_handle_t channel[2];
    if (n == 1) {
        ASSERT_EQ(zx_channel_create(0, &channel[0], end), ZX_OK, "");
        ASSERT_EQ(zx_channel_write(out, 0u, NULL, 0u, channel, 1u), ZX_OK, "");
        return true;
    }

    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");
    ASSERT_TRUE(create_and_nest(channel[0], end, n - 1), "");
    ASSERT_EQ(zx_channel_write(out, 0u, NULL, 0u, channel, 2u), ZX_OK, "");

    END_TEST;
}

static int call_server2(void* ptr) {
    zx_handle_t h = (zx_handle_t) (uintptr_t) ptr;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(250)));
    zx_handle_close(h);
    return 0;
}

static bool channel_call2(void) {
    BEGIN_TEST;

    zx_handle_t cli, srv;
    ASSERT_EQ(zx_channel_create(0, &cli, &srv), ZX_OK, "");

    thrd_t t;
    ASSERT_EQ(thrd_create(&t, call_server2, (void*) (uintptr_t) srv), thrd_success, "");

    char msg[8] = { 0, };
    zx_channel_call_args_t args = {
        .wr_bytes = msg,
        .wr_handles = NULL,
        .wr_num_bytes = sizeof(msg),
        .wr_num_handles = 0,
        .rd_bytes = NULL,
        .rd_handles = NULL,
        .rd_num_bytes = 0,
        .rd_num_handles = 0,
    };

    uint32_t act_bytes = 0xffffffff;
    uint32_t act_handles = 0xffffffff;

    zx_status_t rs = ZX_OK;
    zx_status_t r = zx_channel_call(cli, 0, zx_deadline_after(ZX_MSEC(1000)), &args, &act_bytes,
                                    &act_handles, &rs);

    zx_handle_close(cli);

    EXPECT_EQ(r, ZX_ERR_CALL_FAILED, "");
    EXPECT_EQ(rs, ZX_ERR_PEER_CLOSED, "");

    int retv = 0;
    EXPECT_EQ(thrd_join(t, &retv), thrd_success, "");
    EXPECT_EQ(retv, 0, "");

    END_TEST;
}

// SYSCALL_zx_channel_call_finish is an internal system call used in the
// vDSO's implementation of zx_channel_call.  It's not part of the ABI and
// so it's not exported from the vDSO.  It's hard to test the kernel's
// invariants without calling this directly.  So use some chicanery to
// find its address in the vDSO despite it not being public.
//
// The vdso-code.h header file is generated from the vDSO binary.  It gives
// the offsets of the internal functions.  So take a public vDSO function,
// subtract its offset to discover the vDSO base (could do this other ways,
// but this is the simplest), and then add the offset of the internal
// SYSCALL_zx_channel_call_finish function we want to call.
#include "vdso-code.h"
static zx_status_t zx_channel_call_finish(zx_time_t deadline,
                                          const zx_channel_call_args_t* args,
                                          uint32_t* actual_bytes,
                                          uint32_t* actual_handles,
                                          zx_status_t* read_status) {
    uintptr_t vdso_base =
        (uintptr_t)&zx_handle_close - VDSO_SYSCALL_zx_handle_close;
    uintptr_t fnptr = vdso_base + VDSO_SYSCALL_zx_channel_call_finish;
    return (*(__typeof(zx_channel_call_finish)*)fnptr)(
        deadline, args, actual_bytes, actual_handles, read_status);
}

static bool bad_channel_call_finish(void) {
    BEGIN_TEST;

    char msg[8] = { 0, };
    zx_channel_call_args_t args = {
        .wr_bytes = msg,
        .wr_handles = NULL,
        .wr_num_bytes = sizeof(msg),
        .wr_num_handles = 0,
        .rd_bytes = NULL,
        .rd_handles = NULL,
        .rd_num_bytes = 0,
        .rd_num_handles = 0,
    };

    uint32_t act_bytes = 0xffffffff;
    uint32_t act_handles = 0xffffffff;

    // Call channel_call_finish without having had a channel call interrupted
    zx_status_t rs = ZX_OK;
    zx_status_t r = zx_channel_call_finish(zx_deadline_after(ZX_MSEC(1000)), &args, &act_bytes,
                                           &act_handles, &rs);

    EXPECT_EQ(r, ZX_ERR_BAD_STATE, "");
    EXPECT_EQ(rs, ZX_OK, ""); // The syscall leaves this unchanged.

    END_TEST;
}

static bool channel_nest(void) {
    BEGIN_TEST;
    zx_handle_t channel[2];

    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");

    zx_handle_t end;
    // Nest 200 channels, each one in the payload of the previous one. Without
    // the SafeDeleter in fbl_recycle() this blows the kernel stack when calling
    // the destructors.
    ASSERT_TRUE(create_and_nest(channel[0], &end, 200), "");
    EXPECT_EQ(zx_handle_close(channel[1]), ZX_OK, "");
    EXPECT_EQ(zx_object_wait_one(channel[0], ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, NULL), ZX_OK, "");

    EXPECT_EQ(zx_object_wait_one(end, ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE, NULL), ZX_OK, "");
    EXPECT_EQ(zx_handle_close(end), ZX_OK, "");

    EXPECT_EQ(zx_handle_close(channel[0]), ZX_OK, "");

    END_TEST;
}

// Test the case of writing a channel handle to itself.  The kernel
// currently disallows this, because otherwise it would create a reference
// cycle and potentially allow channels to be leaked.
static bool channel_disallow_write_to_self(void) {
    BEGIN_TEST;

    zx_handle_t channel[2];
    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");
    EXPECT_EQ(zx_channel_write(channel[0], 0, NULL, 0, &channel[0], 1),
              ZX_ERR_NOT_SUPPORTED, "");
    // Clean up.
    EXPECT_EQ(zx_handle_close(channel[0]), ZX_OK, "");
    EXPECT_EQ(zx_handle_close(channel[1]), ZX_OK, "");

    END_TEST;
}

static bool channel_read_etc(void) {
    BEGIN_TEST;

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK, "");
    ASSERT_EQ(zx_handle_replace(event,  ZX_RIGHT_SIGNAL | ZX_RIGHT_TRANSFER, &event), ZX_OK, "");

    zx_handle_t fifo[2];
    ASSERT_EQ(zx_fifo_create(32u, 8u, 0u, &fifo[0], &fifo[1]), ZX_OK, "");

    zx_handle_t sent[] = {
        fifo[0],
        event,
        fifo[1]
    };

    zx_handle_t channel[2];
    ASSERT_EQ(zx_channel_create(0, &channel[0], &channel[1]), ZX_OK, "");
    EXPECT_EQ(zx_channel_write(channel[0], 0u, NULL, 0, sent, 3u), ZX_OK, "");

    zx_handle_info_t recv[] = {{}, {}, {}};
    uint32_t actual_bytes;
    uint32_t actual_handles;

    EXPECT_EQ(zx_channel_read_etc(
        channel[1], 0u, NULL, recv, 0u, 3u, &actual_bytes, &actual_handles), ZX_OK, "");

    EXPECT_EQ(actual_bytes, 0u, "");
    EXPECT_EQ(actual_handles, 3u, "");
    EXPECT_EQ(recv[0].type, ZX_OBJ_TYPE_FIFO, "");
    EXPECT_EQ(recv[0].rights, ZX_DEFAULT_FIFO_RIGHTS, "");

    EXPECT_EQ(recv[1].type, ZX_OBJ_TYPE_EVENT, "");
    EXPECT_EQ(recv[1].rights, ZX_RIGHT_SIGNAL | ZX_RIGHT_TRANSFER, "");

    EXPECT_EQ(recv[2].type, ZX_OBJ_TYPE_FIFO, "");
    EXPECT_EQ(recv[2].rights, ZX_DEFAULT_FIFO_RIGHTS, "");

    EXPECT_EQ(zx_handle_close(channel[0]), ZX_OK, "");
    EXPECT_EQ(zx_handle_close(channel[1]), ZX_OK, "");
    EXPECT_EQ(zx_handle_close(recv[0].handle), ZX_OK, "");
    EXPECT_EQ(zx_handle_close(recv[1].handle), ZX_OK, "");
    EXPECT_EQ(zx_handle_close(recv[2].handle), ZX_OK, "");

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
RUN_TEST(channel_call)
RUN_TEST(channel_call2)
RUN_TEST(bad_channel_call_finish)
RUN_TEST(channel_nest)
RUN_TEST(channel_disallow_write_to_self)
RUN_TEST(channel_read_etc)
END_TEST_CASE(channel_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
