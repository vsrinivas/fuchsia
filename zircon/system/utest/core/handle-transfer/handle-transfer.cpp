// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>
#include <time.h>

#include <lib/zx/channel.h>
#include <zxtest/zxtest.h>


// This example tests transferring channel handles through channels. To do so, it:
//   Creates two channels, a and b.
//   Sends message 0 into a_client
//   Sends a_remote into b_client
//   Sends message 1 into a_client
//   Reads a_remote from b_remote (should receive
//   a_remote, possibly with a new value)
//   Sends 2 into a_client
//   Reads from a_remote. Should read 0, 1, 2 in that order.
TEST(HandleTransferTest, OverChannelThenRead) {
    zx::channel a_client, a_remote;
    zx::channel b_client, b_remote;

    ASSERT_OK(zx::channel::create(0, &a_client, &a_remote));
    ASSERT_OK(zx::channel::create(0, &b_client, &b_remote));

    constexpr char kMessage[] = {0, 1, 2};
    ASSERT_OK(a_client.write(0u, &kMessage[0], 1u, nullptr, 0u));
    zx_handle_t a_remote_raw = a_remote.release();
    ASSERT_OK(b_client.write(0u, nullptr, 0u, &a_remote_raw, 1u));

    ASSERT_OK(a_client.write(0u, &kMessage[1], 1u, nullptr, 0u));

    a_remote_raw = ZX_HANDLE_INVALID;
    uint32_t num_bytes = 0u;
    uint32_t num_handles = 1u;
    ASSERT_OK(b_remote.read(0u, nullptr, &a_remote_raw,
              num_bytes, num_handles, &num_bytes, &num_handles));
    ASSERT_EQ(num_handles, 1);
    a_remote.reset(a_remote_raw);
    ASSERT_TRUE(a_remote.is_valid());

    ASSERT_OK(a_client.write(0u, &kMessage[2], 1u, nullptr, 0u));

    for (size_t i = 0; i < fbl::count_of(kMessage); ++i) {
        char incoming_byte;
        num_bytes = 1u;
        num_handles = 0u;
        ASSERT_OK(a_remote.read(0u, &incoming_byte, nullptr, num_bytes,
                  num_handles, nullptr, &num_handles));
        ASSERT_EQ(num_handles, 0);
        ASSERT_EQ(kMessage[i], incoming_byte);
    }
}

struct ThreadArgs {
    zx::channel a_client;
    zx::channel a_remote;
    zx::channel b_client;
    zx::channel b_remote;
    std::atomic<zx_status_t> fail_outgoing_a_0_on_b_1;
    std::atomic<zx_status_t> fail_incoming_a_0_from_b_0;
    std::atomic<uint64_t> num_handles;
};

int DoWork(void* arg) {
    // sleep for 10ms
    // this is race-prone, but until there's a way to wait for a thread to be
    // blocked, there's no better way to determine that the other thread has
    // entered handle_wait_one.
    struct timespec t = {
        .tv_sec = 0,
        .tv_nsec = 10 * 1000 * 1000,
    };
    nanosleep(&t, nullptr);

    ThreadArgs *test = reinterpret_cast<ThreadArgs*>(arg);

    // Send a_0 through b_1 to b_0.
    zx_handle_t a_client_raw = test->a_client.release();
    test->fail_outgoing_a_0_on_b_1 = test->b_remote.write(0, nullptr, 0u, &a_client_raw, 1);

    // Read from b_0 into handle_a_0_incoming, thus canceling any waits on a_0.
    uint32_t num_handles = 1;
    test->fail_incoming_a_0_from_b_0 = test->b_client.read(0,
        nullptr, &a_client_raw, 0, num_handles, nullptr, &num_handles);
    test->num_handles = num_handles;

    return 0;
}

// This tests canceling a wait when a handle is transferred.
//   There are two channels:
//       channel a with endpoints a_client and a_remote
//   and
//       channel b with endpoints b_client and b_remote
//   A thread is created that sends a_client from b_remote to b_client.
//   main() waits on a_client.
//   The thread then reads from b_client which should cancel the wait in main().
// See [ZX-103].
TEST(HandleTransferTest, CancelsWait) {
    ThreadArgs args;
    ASSERT_OK(zx::channel::create(0, &args.a_client, &args.a_remote));
    ASSERT_OK(zx::channel::create(0, &args.b_client, &args.b_remote));

    thrd_t thr;
    int ret = thrd_create_with_name(&thr, DoWork, &args, "write thread");
    ASSERT_EQ(ret, thrd_success, "failed to create write thread");

    zx_signals_t signals = ZX_CHANNEL_PEER_CLOSED;
    EXPECT_NE(args.a_client.wait_one(signals, zx::deadline_after(zx::sec(1)),
              nullptr), ZX_ERR_TIMED_OUT);
    EXPECT_OK(args.fail_outgoing_a_0_on_b_1.load());
    EXPECT_OK(args.fail_incoming_a_0_from_b_0.load());
    EXPECT_EQ(1u, args.num_handles.load());

    thrd_join(thr, nullptr);
}
