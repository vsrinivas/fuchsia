// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <magenta/compiler.h>
#include <magenta/syscalls.h>
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
    unsigned int packets[2] = {0, 0};
    bool closed[2] = {false, false};
    mx_wait_item_t items[2];
    items[0].handle = channel[0];
    items[1].handle = channel[1];
    items[0].waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    items[1].waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    do {
        status = mx_object_wait_many(items, 2, MX_TIME_INFINITE);
        assert(status == NO_ERROR);
        uint32_t data;
        uint32_t num_bytes = sizeof(uint32_t);
        if (items[0].pending & MX_CHANNEL_READABLE) {
            status = mx_channel_read(channel[0], 0u, &data, NULL,
                                     num_bytes, 0, &num_bytes, NULL);
            assert(status == NO_ERROR);
            packets[0] += 1;
        } else if (items[1].pending & MX_CHANNEL_READABLE) {
            status = mx_channel_read(channel[1], 0u, &data, NULL,
                                     num_bytes, 0, &num_bytes, NULL);
            assert(status == NO_ERROR);
            packets[1] += 1;
        } else {
            if (items[0].pending & MX_CHANNEL_PEER_CLOSED)
                closed[0] = true;
            if (items[1].pending & MX_CHANNEL_PEER_CLOSED)
                closed[1] = true;
        }
    } while (!closed[0] || !closed[1]);
    assert(packets[0] == 3);
    assert(packets[1] == 2);
    return 0;
}

static mx_signals_t get_satisfied_signals(mx_handle_t handle) {
    mx_signals_t pending = 0;
    __UNUSED mx_status_t status = mx_object_wait_one(handle, 0u, 0u, &pending);
    assert(status == ERR_TIMED_OUT);
    return pending;
}

static bool channel_test(void) {
    BEGIN_TEST;

    mx_status_t status;

    mx_handle_t h[2];
    status = mx_channel_create(0, &h[0], &h[1]);
    ASSERT_EQ(status, NO_ERROR, "error in channel create");

    ASSERT_EQ(get_satisfied_signals(h[0]), MX_CHANNEL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    ASSERT_EQ(get_satisfied_signals(h[1]), MX_CHANNEL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    _channel[0] = h[0];
    _channel[2] = h[1];

    static const uint32_t write_data = 0xdeadbeef;
    status = mx_channel_write(_channel[0], 0u, &write_data, sizeof(uint32_t), NULL, 0u);
    ASSERT_EQ(status, NO_ERROR, "error in message write");
    ASSERT_EQ(get_satisfied_signals(
        _channel[0]), MX_CHANNEL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");
    ASSERT_EQ(get_satisfied_signals(
        _channel[2]), MX_CHANNEL_READABLE | MX_CHANNEL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

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
    ASSERT_TRUE((get_satisfied_signals(_channel[3]) & MX_CHANNEL_PEER_CLOSED), "");

    usleep(1);
    mx_handle_close(_channel[0]);

    EXPECT_EQ(thrd_join(thread, NULL), thrd_success, "error in thread join");

    // Since the the other side of _channel[3] is closed, and the read thread read everything
    // from it, the only satisfied/satisfiable signals should be "peer closed".
    ASSERT_EQ(get_satisfied_signals(
        _channel[3]), MX_CHANNEL_PEER_CLOSED | MX_SIGNAL_LAST_HANDLE, "");

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
    status = mx_channel_read(channel[0], 0u, NULL, NULL, 0, 0, NULL, NULL);
    ASSERT_EQ(status, ERR_SHOULD_WAIT, "read on empty non-closed channel produced incorrect error");

    char data = 'x';
    status = mx_channel_write(channel[1], 0u, &data, 1u, NULL, 0u);
    ASSERT_EQ(status, NO_ERROR, "write failed");

    mx_handle_close(channel[1]);

    // Read a message with the peer closed, should yield the message.
    char read_data = '\0';
    uint32_t read_data_size = 1u;
    status = mx_channel_read(channel[0], 0u, &read_data, NULL,
                             read_data_size, 0, &read_data_size, NULL);
    ASSERT_EQ(status, NO_ERROR, "read failed with peer closed but message in the channel");
    ASSERT_EQ(read_data_size, 1u, "read returned incorrect number of bytes");
    ASSERT_EQ(read_data, 'x', "read returned incorrect data");

    // Read from an empty channel with a closed peer, should yield a channel closed error.
    status = mx_channel_read(channel[0], 0u, NULL, NULL, 0, 0, NULL, NULL);
    ASSERT_EQ(status, ERR_PEER_CLOSED, "read on empty closed channel produced incorrect error");

    END_TEST;
}

static bool channel_close_test(void) {
    BEGIN_TEST;
    mx_handle_t channel[2];

    // Channels should gain PEER_CLOSED (and lose WRITABLE) if their peer is closed
    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(channel[1]), NO_ERROR, "");
    ASSERT_EQ(get_satisfied_signals(
        channel[0]), MX_CHANNEL_PEER_CLOSED | MX_SIGNAL_LAST_HANDLE, "");
    ASSERT_EQ(mx_handle_close(channel[0]), NO_ERROR, "");

    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");
    mx_handle_t channel1[2];
    ASSERT_EQ(mx_channel_create(0, &channel1[0], &channel1[1]), NO_ERROR, "");
    mx_handle_t channel2[2];
    ASSERT_EQ(mx_channel_create(0, &channel2[0], &channel2[1]), NO_ERROR, "");

    // Write channel1[0] to channel[0] (to be received by channel[1])
    // and channel2[0] to channel[1] (to be received by channel[0]).
    ASSERT_EQ(mx_channel_write(channel[0], 0u, NULL, 0u, &channel1[0], 1u), NO_ERROR, "");
    channel1[0] = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_channel_write(channel[1], 0u, NULL, 0u, &channel2[0], 1u), NO_ERROR, "");
    channel2[0] = MX_HANDLE_INVALID;

    // Close channel[1]; the former channel1[0] should be closed, so channel1[1] should have
    // peer closed.
    ASSERT_EQ(mx_handle_close(channel[1]), NO_ERROR, "");
    channel[1] = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_object_wait_one(
        channel1[1], MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, NULL), NO_ERROR, "");
    ASSERT_EQ(get_satisfied_signals(
        channel2[1]), MX_CHANNEL_WRITABLE | MX_SIGNAL_LAST_HANDLE, "");

    // Close channel[0]; the former channel2[0] should be closed, so channel2[1]
    // should have peer closed.
    ASSERT_EQ(mx_handle_close(channel[0]), NO_ERROR, "");
    channel[0] = MX_HANDLE_INVALID;
    ASSERT_EQ(get_satisfied_signals(
        channel1[1]), MX_CHANNEL_PEER_CLOSED | MX_SIGNAL_LAST_HANDLE, "");
    ASSERT_EQ(mx_object_wait_one(
        channel2[1], MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, NULL), NO_ERROR, "");

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

    mx_status_t status = mx_object_get_info(event, MX_INFO_HANDLE_BASIC, &event_handle_info,
                                            sizeof(event_handle_info), NULL, NULL);
    ASSERT_EQ(status, NO_ERROR, "failed to get event info");
    mx_rights_t initial_event_rights = event_handle_info.rights;
    mx_handle_t non_transferable_event;
    mx_handle_duplicate(
        event, initial_event_rights & ~MX_RIGHT_TRANSFER, &non_transferable_event);

    mx_status_t write_result = mx_channel_write(
        channel[0], 0u, NULL, 0, &non_transferable_event, 1u);
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
        mx_status_t status = mx_channel_read(_channel[0], 0u, &msg, NULL,
                                             msg_size, 0, &msg_size, NULL);
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
        mx_status_t status = mx_handle_duplicate(handle, MX_RIGHT_TRANSFER, &handles[i]);
        assert(status == NO_ERROR);
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

    EXPECT_EQ(mx_object_wait_one(channel[1], MX_CHANNEL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    write_test_message(channel[0], event, 10u, 0u);
    EXPECT_EQ(mx_channel_read(channel[1], MX_CHANNEL_READ_MAY_DISCARD, NULL, NULL, 0, 0, NULL, NULL),
              ERR_BUFFER_TOO_SMALL, "");

    EXPECT_EQ(mx_object_wait_one(channel[1], MX_CHANNEL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    char data[1000];
    uint32_t size;

    write_test_message(channel[0], event, 100u, 0u);
    size = 10u;
    EXPECT_EQ(mx_channel_read(channel[1], MX_CHANNEL_READ_MAY_DISCARD, data, NULL, size, 0, &size, NULL),
              ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 100u, "wrong size");

    EXPECT_EQ(mx_object_wait_one(channel[1], MX_CHANNEL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    mx_handle_t handles[10];
    uint32_t num_handles;

    write_test_message(channel[0], event, 0u, 5u);
    size = 10u;
    num_handles = 1u;
    EXPECT_EQ(mx_channel_read(channel[1], MX_CHANNEL_READ_MAY_DISCARD, data, handles,
                              size, num_handles, &size, &num_handles),
              ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 0u, "wrong size");
    EXPECT_EQ(num_handles, 5u, "wrong number of handles");

    EXPECT_EQ(mx_object_wait_one(channel[1], MX_CHANNEL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    write_test_message(channel[0], event, 100u, 5u);
    size = 10u;
    num_handles = 1u;
    EXPECT_EQ(mx_channel_read(channel[1], MX_CHANNEL_READ_MAY_DISCARD, data, handles,
                              size, num_handles, &size, &num_handles),
              ERR_BUFFER_TOO_SMALL, "");
    EXPECT_EQ(size, 100u, "wrong size");
    EXPECT_EQ(num_handles, 5u, "wrong number of handles");

    EXPECT_EQ(mx_object_wait_one(channel[1], MX_CHANNEL_READABLE, 0u, NULL), ERR_TIMED_OUT, "");

    mx_status_t close_result = mx_handle_close(event);
    EXPECT_EQ(close_result, NO_ERROR, "");
    close_result = mx_handle_close(channel[0]);
    EXPECT_EQ(close_result, NO_ERROR, "");
    close_result = mx_handle_close(channel[1]);
    EXPECT_EQ(close_result, NO_ERROR, "");

    END_TEST;
}


static uint32_t call_test_done = 0;
static mtx_t call_test_lock;
static cnd_t call_test_cvar;

// we use txid_t for cmd here so that the test
// works with both 32bit and 64bit txids
typedef struct {
    mx_txid_t txid;
    mx_txid_t cmd;
    uint32_t bit;
    unsigned action;
    mx_status_t expect;
    mx_status_t expect_rs;
    const char* name;
    const char* err;
    int val;
    mx_handle_t h;
    thrd_t t;
} ccargs_t;

#define SRV_SEND_HANDLE   0x0001
#define SRV_SEND_DATA     0x0002
#define SRV_DISCARD       0x0004
#define CLI_SHORT_WAIT    0x0100
#define CLI_RECV_HANDLE   0x0200
#define CLI_SEND_HANDLE   0x0400

static int call_client(void* _args) {
    ccargs_t* ccargs = _args;
    mx_channel_call_args_t args;

    mx_txid_t data[2];
    mx_handle_t txhandle = 0;
    mx_handle_t rxhandle = 0;

    mx_status_t r;
    if (ccargs->action & CLI_SEND_HANDLE) {
        if ((r = mx_event_create(0, &txhandle)) != NO_ERROR) {
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

    mx_time_t deadline = (ccargs->action & CLI_SHORT_WAIT) ? mx_deadline_after(MX_MSEC(250)) :
            MX_TIME_INFINITE;
    mx_status_t rs = NO_ERROR;
    if ((r = mx_channel_call(ccargs->h, 0, deadline, &args, &act_bytes, &act_handles, &rs)) != ccargs->expect) {
        ccargs->err = "channel call returned";
        ccargs->val = r;
    }
    if (txhandle && (r < 0)) {
        mx_handle_close(txhandle);
    }
    if (rxhandle) {
        mx_handle_close(rxhandle);
    }
    if (r == ERR_CALL_FAILED) {
        if (ccargs->expect_rs && (ccargs->expect_rs != rs)) {
            ccargs->err = "read_status not what was expected";
            ccargs->val = ccargs->expect_rs;
        }
    }
    if (r == NO_ERROR) {
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
    }

done:
    mtx_lock(&call_test_lock);
    call_test_done |= ccargs->bit;
    cnd_broadcast(&call_test_cvar);
    mtx_unlock(&call_test_lock);
    return 0;
}

static ccargs_t ccargs[] = {
    {
        .name = "too large reply",
        .action = SRV_SEND_DATA,
        .expect = ERR_CALL_FAILED,
        .expect_rs = ERR_BUFFER_TOO_SMALL,
    },
    {
        .name = "no reply",
        .action = SRV_DISCARD | CLI_SHORT_WAIT,
        .expect = ERR_TIMED_OUT,
    },
    {
        .name = "reply handle",
        .action = SRV_SEND_HANDLE | CLI_RECV_HANDLE,
    },
    {
        .name = "unwanted reply handle",
        .action = SRV_SEND_HANDLE,
        .expect = ERR_CALL_FAILED,
        .expect_rs = ERR_BUFFER_TOO_SMALL,
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
    mx_handle_t h = (mx_handle_t) (uintptr_t) ptr;

    ccargs_t msg[countof(ccargs)];
    memset(msg, 0, sizeof(msg));

    // received the expected number of messages
    for (unsigned n = 0; n < countof(ccargs); n++) {
        mx_object_wait_one(h, MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, NULL);

        uint32_t bytes = sizeof(msg[0]);
        uint32_t handles = 1;
        mx_handle_t handle = 0;
        if (mx_channel_read(h, 0, &msg[n], &handle, bytes, handles, &bytes, &handles) != NO_ERROR) {
            fprintf(stderr, "call_server() read failed\n");
            break;
        }
        if (handle) {
            mx_handle_close(handle);
        }
    }

    // reply to them in reverse order received
    for (unsigned n = 0; n < countof(ccargs); n++) {
        ccargs_t* m = &msg[countof(ccargs) - n - 1];

        if (m->action & SRV_DISCARD) {
            continue;
        }

        mx_txid_t data[4];
        data[0] = m->txid;
        data[1] = m->txid * 31337;
        data[2] = 0x22222222;
        data[3] = 0x33333333;

        uint32_t bytes = sizeof(mx_txid_t) * ((m->action & SRV_SEND_DATA) ? 4 : 2);
        uint32_t handles = (m->action & SRV_SEND_HANDLE) ? 1 : 0;
        mx_handle_t handle = 0;
        if (handles) {
            mx_event_create(0, &handle);
        }
        if (mx_channel_write(h, 0, data, bytes, &handle, handles) != NO_ERROR) {
            fprintf(stderr, "call_server() write failed\n");
            break;
        }
    }
    return 0;
}

static bool channel_call(void) {
    BEGIN_TEST;

    mtx_init(&call_test_lock, mtx_plain);
    cnd_init(&call_test_cvar);

    mx_handle_t cli, srv;
    ASSERT_EQ(mx_channel_create(0, &cli, &srv), NO_ERROR, "");

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

    // wait for all tests to finish or timeout
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += 5;
    int r = 0;
    while (r == 0) {
        mtx_lock(&call_test_lock);
        if (call_test_done == waitfor) {
            r = -1;
        } else {
            r = cnd_timedwait(&call_test_cvar, &call_test_lock, &until);
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

    mx_handle_close(cli);
    mx_handle_close(srv);
    END_TEST;
}

static bool create_and_nest(mx_handle_t out, mx_handle_t* end, size_t n) {
    BEGIN_TEST;

    mx_handle_t channel[2];
    if (n == 1) {
        ASSERT_EQ(mx_channel_create(0, &channel[0], end), NO_ERROR, "");
        ASSERT_EQ(mx_channel_write(out, 0u, NULL, 0u, channel, 1u), NO_ERROR, "");
        return true;
    }

    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");
    ASSERT_TRUE(create_and_nest(channel[0], end, n - 1), "");
    ASSERT_EQ(mx_channel_write(out, 0u, NULL, 0u, channel, 2u), NO_ERROR, "");

    END_TEST;
}

static int call_server2(void* ptr) {
    mx_handle_t h = (mx_handle_t) (uintptr_t) ptr;
    mx_nanosleep(mx_deadline_after(MX_MSEC(250)));
    mx_handle_close(h);
    return 0;
}

static bool channel_call2(void) {
    BEGIN_TEST;

    mx_handle_t cli, srv;
    ASSERT_EQ(mx_channel_create(0, &cli, &srv), NO_ERROR, "");

    thrd_t t;
    ASSERT_EQ(thrd_create(&t, call_server2, (void*) (uintptr_t) srv), thrd_success, "");

    char msg[8] = { 0, };
    mx_channel_call_args_t args = {
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

    mx_status_t rs = NO_ERROR;
    mx_status_t r = mx_channel_call(cli, 0, mx_deadline_after(MX_MSEC(1000)), &args, &act_bytes,
                                    &act_handles, &rs);

    mx_handle_close(cli);

    EXPECT_EQ(r, ERR_CALL_FAILED, "");
    EXPECT_EQ(rs, ERR_PEER_CLOSED, "");

    END_TEST;
}

static bool channel_nest(void) {
    BEGIN_TEST;
    mx_handle_t channel[2];

    ASSERT_EQ(mx_channel_create(0, &channel[0], &channel[1]), NO_ERROR, "");

    mx_handle_t end;
    ASSERT_TRUE(create_and_nest(channel[0], &end, 10), "");
    EXPECT_EQ(mx_handle_close(channel[1]), NO_ERROR, "");
    EXPECT_EQ(mx_object_wait_one(channel[0], MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, NULL), NO_ERROR, "");

    EXPECT_EQ(mx_object_wait_one(end, MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, NULL), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(end), NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(channel[0]), NO_ERROR, "");

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
RUN_TEST(channel_nest)
END_TEST_CASE(channel_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
