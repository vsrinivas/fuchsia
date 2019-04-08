// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <stdio.h>
#include <threads.h>

#include <fbl/algorithm.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <unittest/unittest.h>

static bool basic_test(void) {
    BEGIN_TEST;
    zx_status_t status;

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, 0, "could not create port");

    const zx_port_packet_t in = {
        12ull,
        ZX_PKT_TYPE_USER + 5u,    // kernel overrides the |type|.
        -3,
        { {} }
    };

    zx_port_packet_t out = {};

    status = zx_port_queue(port, nullptr);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);

    status = zx_port_queue(port, &in);
    EXPECT_EQ(status, ZX_OK);

    status = zx_port_wait(port, ZX_TIME_INFINITE, &out);
    EXPECT_EQ(status, ZX_OK);

    EXPECT_EQ(out.key, 12u);
    EXPECT_EQ(out.type, ZX_PKT_TYPE_USER);
    EXPECT_EQ(out.status, -3);

    EXPECT_EQ(memcmp(&in.user, &out.user, sizeof(zx_port_packet_t::user)), 0);

    status = zx_handle_close(port);
    EXPECT_EQ(status, ZX_OK);

    END_TEST;
}

static bool queue_and_close_test(void) {
    BEGIN_TEST;
    zx_status_t status;

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, ZX_OK, "could not create port");

    zx_port_packet_t out0 = {};
    status = zx_port_wait(port, zx_deadline_after(ZX_USEC(1)), &out0);
    EXPECT_EQ(status, ZX_ERR_TIMED_OUT);

    const zx_port_packet_t in = {
        1ull,
        ZX_PKT_TYPE_USER,
        0,
        { {} }
    };

    status = zx_port_queue(port, &in);
    EXPECT_EQ(status, ZX_OK);

    status = zx_handle_close(port);
    EXPECT_EQ(status, ZX_OK);

    END_TEST;
}

static bool queue_too_many(void) {
    BEGIN_TEST;
    zx_status_t status;

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, ZX_OK, "could not create port");

    const zx_port_packet_t in = {
        2ull,
        ZX_PKT_TYPE_USER,
        0,
        { {} }
    };

    size_t count;
    for (count = 0; count < 5000u; ++count) {
        status = zx_port_queue(port, &in);
        if (status != ZX_OK)
            break;
    }

    EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT);
    EXPECT_EQ(count, 2049u);

    status = zx_handle_close(port);
    EXPECT_EQ(status, ZX_OK);

    END_TEST;
}

static bool async_wait_channel_test(void) {
    BEGIN_TEST;
    zx_status_t status;

    const uint64_t key0 = 6567ull;

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, ZX_OK);

    zx_handle_t ch[2];
    status = zx_channel_create(0u, &ch[0], &ch[1]);
    EXPECT_EQ(status, ZX_OK);

    for (int ix = 0; ix != 5; ++ix) {
        zx_port_packet_t out = {};
        status = zx_object_wait_async(ch[1], port, key0, ZX_CHANNEL_READABLE, ZX_WAIT_ASYNC_ONCE);
        EXPECT_EQ(status, ZX_OK);

        status = zx_port_wait(port, zx_deadline_after(ZX_USEC(200)), &out);
        EXPECT_EQ(status, ZX_ERR_TIMED_OUT);

        status = zx_channel_write(ch[0], 0u, "here", 4, nullptr, 0u);
        EXPECT_EQ(status, ZX_OK);

        status = zx_port_wait(port, ZX_TIME_INFINITE, &out);
        EXPECT_EQ(status, ZX_OK);

        EXPECT_EQ(out.key, key0);
        EXPECT_EQ(out.type, ZX_PKT_TYPE_SIGNAL_ONE);
        EXPECT_EQ(out.signal.observed,
            ZX_CHANNEL_WRITABLE | ZX_CHANNEL_READABLE);
        EXPECT_EQ(out.signal.trigger, ZX_CHANNEL_READABLE);
        EXPECT_EQ(out.signal.count, 1u);

        status = zx_channel_read(ch[1], ZX_CHANNEL_READ_MAY_DISCARD,
                                 nullptr, nullptr, 0u, 0, nullptr, nullptr);
        EXPECT_EQ(status, ZX_ERR_BUFFER_TOO_SMALL);
    }

    zx_port_packet_t out1 = {};

    status = zx_port_wait(port, zx_deadline_after(ZX_USEC(200)), &out1);
    EXPECT_EQ(status, ZX_ERR_TIMED_OUT);

    status = zx_object_wait_async(ch[1], port, key0, ZX_CHANNEL_READABLE, ZX_WAIT_ASYNC_ONCE);
    EXPECT_EQ(status, ZX_OK);

    status = zx_handle_close(ch[1]);
    EXPECT_EQ(status, ZX_OK);

    status = zx_handle_close(ch[0]);
    EXPECT_EQ(status, ZX_OK);

    status = zx_handle_close(port);
    EXPECT_EQ(status, ZX_OK);

    END_TEST;
}

static bool async_wait_close_order(const int order[3], uint32_t wait_option) {
    BEGIN_TEST;
    zx_status_t status;

    const uint64_t key0 = 1122ull;

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, ZX_OK);

    zx_handle_t ch[2];
    status = zx_channel_create(0u, &ch[0], &ch[1]);
    EXPECT_EQ(status, ZX_OK);

    status = zx_object_wait_async(ch[1], port, key0,
        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, wait_option);
    EXPECT_EQ(status, ZX_OK);

    for (int ix = 0; ix != 3; ++ix) {
        switch (order[ix]) {
        case 0: status = zx_handle_close(ch[1]); break;
        case 1: status = zx_handle_close(ch[0]); break;
        case 2: status = zx_handle_close(port); break;
        }
        EXPECT_EQ(status, ZX_OK);
    }

    END_TEST;
}

static bool async_wait_close_order_1() {
    int order[] = {0, 1, 2};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE);
}

static bool async_wait_close_order_2() {
    int order[] = {0, 2, 1};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE);
}

static bool async_wait_close_order_3() {
    int order[] = {1, 2, 0};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE);
}

static bool async_wait_close_order_4() {
    int order[] = {1, 0, 2};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE);
}

static bool async_wait_close_order_5() {
    int order[] = {2, 1, 0};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE);
}

static bool async_wait_close_order_6() {
    int order[] = {2, 0, 1};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE);
}

static bool async_wait_event_test_single(void) {
    BEGIN_TEST;
    zx_status_t status;

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, ZX_OK);

    zx_handle_t ev;
    status = zx_event_create(0u, &ev);
    EXPECT_EQ(status, ZX_OK);

    const uint32_t kNumAwaits = 7;

    for (uint32_t ix = 0; ix != kNumAwaits; ++ix) {
        status = zx_object_wait_async(ev, port, ix, ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_ONCE);
        EXPECT_EQ(status, ZX_OK);
    }

    status = zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED);
    EXPECT_EQ(status, ZX_OK);

    zx_port_packet_t out = {};
    uint64_t key_sum = 0u;

    for (uint32_t ix = 0; ix != (kNumAwaits - 2); ++ix) {
        EXPECT_EQ(status, ZX_OK);
        status = zx_port_wait(port, ZX_TIME_INFINITE, &out);
        EXPECT_EQ(status, ZX_OK);
        key_sum += out.key;
        EXPECT_EQ(out.type, ZX_PKT_TYPE_SIGNAL_ONE);
        EXPECT_EQ(out.signal.count, 1u);
    }

    EXPECT_EQ(key_sum, 20u);

    // The port has packets left in it.
    status = zx_handle_close(port);
    EXPECT_EQ(status, ZX_OK);

    status = zx_handle_close(ev);
    EXPECT_EQ(status, ZX_OK);

    END_TEST;
}

static bool async_wait_event_test_repeat(void) {
    BEGIN_TEST;
    zx_status_t status;

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, ZX_OK);

    zx_handle_t ev;
    status = zx_event_create(0u, &ev);
    EXPECT_EQ(status, ZX_OK);

    const uint64_t key0 = 1122ull;

    zx_port_packet_t out = {};
    uint64_t count[3] = {};

    for (int ix = 0; ix != 24; ++ix) {
        status = zx_object_wait_async(ev, port, key0,
            ZX_EVENT_SIGNALED | ZX_USER_SIGNAL_2, ZX_WAIT_ASYNC_ONCE);
        EXPECT_EQ(status, ZX_OK);

        uint32_t ub = (ix % 2) ? 0u : ZX_USER_SIGNAL_2;
        EXPECT_EQ(zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED | ub), ZX_OK);
        EXPECT_EQ(zx_object_signal(ev, ZX_EVENT_SIGNALED | ub, 0u), ZX_OK);

        ASSERT_EQ(zx_port_wait(port, 0ull, &out), ZX_OK);
        ASSERT_EQ(out.type, ZX_PKT_TYPE_SIGNAL_ONE);
        ASSERT_EQ(out.signal.count, 1u);
        count[0] += (out.signal.observed & ZX_EVENT_SIGNALED) ? 1 : 0;
        count[1] += (out.signal.observed & ZX_USER_SIGNAL_2) ? 1 : 0;
        count[2] += (out.signal.observed &
            ~(ZX_EVENT_SIGNALED|ZX_USER_SIGNAL_2)) ? 1 : 0;
    }

    EXPECT_EQ(count[0], 24u);
    EXPECT_EQ(count[1], 12u);
    EXPECT_EQ(count[2], 0u);

    status = zx_handle_close(port);
    EXPECT_EQ(status, ZX_OK);

    status = zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED | ZX_USER_SIGNAL_2);
    EXPECT_EQ(status, ZX_OK);

    status = zx_handle_close(ev);
    EXPECT_EQ(status, ZX_OK);

    END_TEST;
}

// Check that zx_object_wait_async() returns an error if it is passed an
// invalid option.
static bool async_wait_invalid_option() {
    BEGIN_TEST;
    zx_handle_t port;
    ASSERT_EQ(zx_port_create(0, &port), ZX_OK);
    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0u, &event), ZX_OK);
    const uint64_t kKey = 0;
    const uint32_t kInvalidOption = ZX_WAIT_ASYNC_ONCE + 2;
    EXPECT_EQ(zx_object_wait_async(event, port, kKey, ZX_EVENT_SIGNALED,
                                   kInvalidOption), ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx_handle_close(event), ZX_OK);
    ASSERT_EQ(zx_handle_close(port), ZX_OK);
    END_TEST;
}

static bool channel_pre_writes_test() {
    BEGIN_TEST;
    zx_status_t status;

    const uint64_t key0 = 65667ull;

    zx_handle_t ch[2];
    status = zx_channel_create(0u, &ch[0], &ch[1]);
    EXPECT_EQ(status, ZX_OK);

    for (int ix = 0; ix != 5; ++ix) {
        EXPECT_EQ(zx_channel_write(ch[0], 0u, "123456", 6, nullptr, 0u), ZX_OK);
    }

    status = zx_handle_close(ch[0]);
    EXPECT_EQ(status, ZX_OK);

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, ZX_OK);

    status = zx_object_wait_async(ch[1], port, key0,
        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, ZX_WAIT_ASYNC_ONCE);
    EXPECT_EQ(status, ZX_OK);

    zx_port_packet_t out = {};
    int wait_count = 0;
    uint64_t read_count = 0u;

    while (true) {
        status = zx_port_wait(port, 0ull, &out);
        if (status != ZX_OK)
            break;
        wait_count++;
        if (out.signal.observed != ZX_CHANNEL_PEER_CLOSED)
            read_count += out.signal.count;
        EXPECT_NE(out.signal.count, 0u);
    }

    EXPECT_EQ(wait_count, 1u);
    EXPECT_EQ(out.signal.trigger, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    EXPECT_EQ(read_count, 5u);

    status = zx_handle_close(port);
    EXPECT_EQ(status, ZX_OK);

    status = zx_handle_close(ch[1]);
    EXPECT_EQ(status, ZX_OK);

    END_TEST;
}

static bool cancel_event_key() {
    BEGIN_TEST;
    zx_status_t status;

    zx_handle_t port;
    zx_handle_t ev;

    EXPECT_EQ(zx_port_create(0, &port), ZX_OK);
    EXPECT_EQ(zx_event_create(0u, &ev), ZX_OK);

    // Notice repeated key below.
    const uint64_t keys[] = {128u, 13u, 7u, 13u};

    for (uint32_t ix = 0; ix != fbl::count_of(keys); ++ix) {
        EXPECT_EQ(zx_object_wait_async(
            ev, port, keys[ix], ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_ONCE), ZX_OK);
    }

    // We cancel before it is signaled so no packets from |13| are seen.
    EXPECT_EQ(zx_port_cancel(port, ev, 13u), ZX_OK);

    for (int ix = 0; ix != 2; ++ix) {
        EXPECT_EQ(zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED), ZX_OK);
        EXPECT_EQ(zx_object_signal(ev, ZX_EVENT_SIGNALED, 0u), ZX_OK);
    }

    zx_port_packet_t out = {};
    int wait_count = 0;
    uint64_t key_sum = 0;

    while (true) {
        status = zx_port_wait(port, 0ull, &out);
        if (status != ZX_OK)
            break;
        wait_count++;
        key_sum += out.key;
        EXPECT_EQ(out.signal.trigger, ZX_EVENT_SIGNALED);
        EXPECT_EQ(out.signal.observed, ZX_EVENT_SIGNALED);
    }

    // We cancel after the packet has been delivered.
    EXPECT_EQ(zx_port_cancel(port, ev, 128u), ZX_ERR_NOT_FOUND);

    EXPECT_EQ(wait_count, 2);
    EXPECT_EQ(key_sum, keys[0] + keys[2]);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);
    EXPECT_EQ(zx_handle_close(ev), ZX_OK);
    END_TEST;
}

static bool cancel_event_key_after() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t port;

    EXPECT_EQ(zx_port_create(0, &port), ZX_OK);

    zx_handle_t ev[3];
    const uint64_t keys[] = {128u, 3u, 3u};

    for (uint32_t ix = 0; ix != fbl::count_of(keys); ++ix) {

        EXPECT_EQ(zx_event_create(0u, &ev[ix]), ZX_OK);
        EXPECT_EQ(zx_object_wait_async(
            ev[ix], port, keys[ix], ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_ONCE), ZX_OK);
    }

    EXPECT_EQ(zx_object_signal(ev[0], 0u, ZX_EVENT_SIGNALED), ZX_OK);
    EXPECT_EQ(zx_object_signal(ev[1], 0u, ZX_EVENT_SIGNALED), ZX_OK);

    // We cancel after the first two signals and before the third. So it should
    // test both cases with queued packets and no-yet-fired packets.
    EXPECT_EQ(zx_port_cancel(port, ev[1], 3u), ZX_OK);
    EXPECT_EQ(zx_port_cancel(port, ev[2], 3u), ZX_OK);

    EXPECT_EQ(zx_object_signal(ev[2], 0u, ZX_EVENT_SIGNALED), ZX_OK);

    zx_port_packet_t out = {};
    int wait_count = 0;
    uint64_t key_sum = 0;

    while (true) {
        status = zx_port_wait(port, 0ull, &out);
        if (status != ZX_OK)
            break;
        wait_count++;
        key_sum += out.key;
        EXPECT_EQ(out.signal.trigger, ZX_EVENT_SIGNALED);
        EXPECT_EQ(out.signal.observed, ZX_EVENT_SIGNALED);
    }

    EXPECT_EQ(wait_count, 1);
    EXPECT_EQ(key_sum, keys[0]);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);
    EXPECT_EQ(zx_handle_close(ev[0]), ZX_OK);
    EXPECT_EQ(zx_handle_close(ev[1]), ZX_OK);
    END_TEST;
}


struct test_context {
    zx_handle_t port;
    uint32_t count;
};

static int port_reader_thread(void* arg) {
    auto ctx = reinterpret_cast<test_context*>(arg);
    zx_port_packet_t out = {};
    uint64_t received = 0;
    do {
        auto st = zx_port_wait(ctx->port, ZX_TIME_INFINITE, &out);
        if (st < 0)
            return st;
        ++received;
    } while (received < ctx->count);
    return 0;
}

static bool threads_event() {
    BEGIN_TEST;

    zx_handle_t port;
    zx_handle_t ev;

    EXPECT_EQ(zx_port_create(0, &port), ZX_OK);
    EXPECT_EQ(zx_event_create(0u, &ev), ZX_OK);

    thrd_t threads[3];
    test_context ctx[3];
    for (size_t ix = 0; ix != fbl::count_of(threads); ++ix) {
        // |count| is one so each thread is going to pick one packet each
        // and exit. See bug ZX-648 for the case this is testing.
        ctx[ix] = { port, 1u };

        EXPECT_EQ(zx_object_wait_async(
                  ev, port, (500u + ix), ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_ONCE), ZX_OK);
        EXPECT_EQ(thrd_create(&threads[ix], port_reader_thread, &ctx[ix]),
                  thrd_success);
    }

    EXPECT_EQ(zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED), ZX_OK);

    for (size_t ix = 0; ix != fbl::count_of(threads); ++ix) {
        int res;
        EXPECT_EQ(thrd_join(threads[ix], &res), thrd_success);
        EXPECT_EQ(res, 0);
        EXPECT_EQ(ctx[ix].count, 1u);
    }

    EXPECT_EQ(zx_handle_close(port), ZX_OK);
    EXPECT_EQ(zx_handle_close(ev), ZX_OK);

    END_TEST;
}


static constexpr uint32_t kStressCount = 20000u;
static constexpr uint64_t kSleeps[] = { 0, 10, 2, 0, 15, 0};

static int signaler_thread(void* arg) {
    auto ev = *reinterpret_cast<zx_handle_t*>(arg);

    uint64_t count = 0;

    while (true) {
        auto st = zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED);
        if (st != ZX_OK)
            return 1;
        auto duration = kSleeps[count % fbl::count_of(kSleeps)];
        if (duration > 0)
            zx_nanosleep(zx_deadline_after(duration));
        st = zx_object_signal(ev, ZX_EVENT_SIGNALED, 0u);
        if (st != ZX_OK)
            return 1;

        ++count;
    }

    return 0;
}

static int waiter_thread(void* arg) {
    auto ob = reinterpret_cast<zx_handle_t*>(arg);
    auto& port = ob[0];
    auto& ev   = ob[1];
    const auto key = 919u;

    zx_status_t st = ZX_OK;;

    auto count = kStressCount;
    while (--count) {
        st = zx_object_wait_async(ev, port, key, ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_ONCE);
        if (st != ZX_OK)
            break;

        zx_signals_t observed;
        st = zx_object_wait_one(ev, ZX_EVENT_SIGNALED, ZX_TIME_INFINITE, &observed);
        if (st != ZX_OK)
            break;

        st = zx_port_cancel(port, ev, key);
        if (st != ZX_OK)
            break;
    }

    // Done, close the event so the other thread exits.
    zx_handle_close(ev);
    return st;
}


static bool cancel_stress() {
    BEGIN_TEST;

    // This tests a race that existed between the port observer
    // removing itself from the event and the cancellation logic which is
    // also working with the same internal object. The net effect of the
    // bug is that port_cancel() would fail with ZX_ERR_NOT_FOUND.
    //
    // When running on real hardware or KVM-accelerated emulation
    // a good number to set for kStressCount is 50000000.

    zx_handle_t ob[2];

    EXPECT_EQ(zx_port_create(0, &ob[0]), ZX_OK);
    EXPECT_EQ(zx_event_create(0u, &ob[1]), ZX_OK);

    thrd_t thread[2];
    EXPECT_EQ(thrd_create(&thread[0], waiter_thread, ob), thrd_success);
    EXPECT_EQ(thrd_create(&thread[1], signaler_thread, &ob[1]), thrd_success);

    int res;
    EXPECT_EQ(thrd_join(thread[0], &res), thrd_success, "waiter");
    EXPECT_EQ(res, ZX_OK);

    EXPECT_EQ(thrd_join(thread[1], &res), thrd_success, "signaler");
    EXPECT_EQ(res, 1);

    END_TEST;
}

// A stress test that repeatedly signals and closes events registered with a port.
static bool signal_close_stress() {
    BEGIN_TEST;

    constexpr zx_duration_t kTestDuration = ZX_SEC(1);
    srand(4);

    // Continually reads packets from a port until it gets a ZX_PKT_TYPE_USER.
    auto drainer_thread = [](void* arg) -> int {
        auto port = *reinterpret_cast<zx_handle_t*>(arg);
        while (true) {
            zx_port_packet_t packet{};
            zx_status_t status = zx_port_wait(port, ZX_TIME_INFINITE, &packet);
            if (status != ZX_OK) {
                return status;
            }
            if (packet.type == ZX_PKT_TYPE_USER) {
                break;
            }
        }
        return ZX_OK;
    };

    std::atomic<bool> keep_running(true);

    struct signaler_args {
        zx_handle_t port;
        std::atomic<bool>* keep_running;
    };

    // Creates an event registered with the port then performs the following actions randomly:
    //   a. sleep
    //   b. signal the event
    //   c. signal the event, then close it
    auto signaler_thread = [](void* arg) -> int {
        auto args = reinterpret_cast<signaler_args*>(arg);
        auto port = args->port;
        auto keep_running = args->keep_running;

        zx_handle_t ev = ZX_HANDLE_INVALID;
        zx_status_t status = ZX_OK;
        while (keep_running->load()) {
            if (ev == ZX_HANDLE_INVALID) {
                status = zx_event_create(0u, &ev);
                if (status != ZX_OK) {
                    return status;
                }
                status = zx_object_wait_async(ev, port, 0, ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_ONCE);
                if (status != ZX_OK) {
                    return status;
                }
            }

            unsigned action = rand() % 3;
            switch (action) {
            case 0: // sleep
                zx_nanosleep(ZX_MSEC(1));
                break;
            case 1: // signal
                status = zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED);
                if (status != ZX_OK) {
                    return status;
                }
                break;
            default: // signal and close
                status = zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED);
                if (status != ZX_OK) {
                    return status;
                }
                status = zx_handle_close(ev);
                ev = ZX_HANDLE_INVALID;
            }
        }

        return zx_handle_close(ev);
    };

    zx_handle_t port;
    zx_status_t status = zx_port_create(0, &port);
    ASSERT_EQ(status, ZX_OK);

    constexpr unsigned kNumSignalers = 4;
    thrd_t signalers[kNumSignalers];
    signaler_args args{port, &keep_running};
    for (auto& t : signalers) {
        ASSERT_EQ(thrd_create(&t, signaler_thread, &args), thrd_success);
    }

    constexpr unsigned kNumDrainers = 4;
    thrd_t drainers[kNumDrainers];
    for (auto& t : drainers) {
        ASSERT_EQ(thrd_create(&t, drainer_thread, &port), thrd_success);
    }

    zx_nanosleep(zx_deadline_after(kTestDuration));
    keep_running.store(false);

    for (unsigned i = 0; i < kNumDrainers; ++i) {
        zx_port_packet_t pkt{};
        pkt.type = ZX_PKT_TYPE_USER;
        zx_status_t status;
        do {
            status = zx_port_queue(port, &pkt);
        } while (status == ZX_ERR_SHOULD_WAIT);
        ASSERT_EQ(status, ZX_OK);
    }

    for (auto& t : drainers) {
        int res;
        ASSERT_EQ(thrd_join(t, &res), thrd_success);
        ASSERT_EQ(res, ZX_OK);
    }

    for (auto& t : signalers) {
        int res;
        ASSERT_EQ(thrd_join(t, &res), thrd_success);
        ASSERT_EQ(res, ZX_OK);
    }

    status = zx_handle_close(port);
    ASSERT_EQ(status, ZX_OK);

    END_TEST;
}

// A stress test designed to create a race where one thread is closing the port as another thread is
// performing an object_wait_async using the same port handle.
static bool port_close_wait_race_stress() {
    BEGIN_TEST;

    constexpr zx_duration_t kTestDuration = ZX_SEC(1);
    srand(4);

    struct args_t {
        std::atomic<bool>* keep_running;
        std::atomic<zx_handle_t>* port;
        zx_handle_t event;
    };

    // Repeatedly asynchronously wait on an event.
    auto waiter_thread = [](void* arg) -> int {
        auto args = reinterpret_cast<args_t*>(arg);
        auto keep_running = args->keep_running;
        auto port = args->port;
        auto event = args->event;

        while (keep_running->load()) {
            zx_status_t status =
                zx_object_wait_async(event, port->load(), 0, ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_ONCE);
            if (status != ZX_OK && status != ZX_ERR_BAD_HANDLE) {
                return status;
            }
        }

        return ZX_OK;
    };

    // Repeatedly create and close a port.
    auto closer_thread = [](void* arg) -> int {
        auto args = reinterpret_cast<args_t*>(arg);
        auto port = args->port;
        auto keep_running = args->keep_running;

        while (keep_running->load()) {
            zx_handle_t handle;
            zx_status_t status = zx_port_create(0, &handle);
            if (status != ZX_OK) {
                return status;
            }
            port->store(handle);

            // Give the waiter threads an opportunity to get the handle and wait_async on it.
            zx_nanosleep(ZX_MSEC(10));

            // Then close it out from under them.
            status = zx_handle_close(handle);
            port->store(ZX_HANDLE_INVALID);
            if (status != ZX_OK) {
                return status;
            }
        }
        return ZX_OK;
    };

    std::atomic<bool> keep_running(true);
    std::atomic<zx_handle_t> port(ZX_HANDLE_INVALID);
    args_t args{&keep_running, &port, ZX_HANDLE_INVALID};

    zx_status_t status = zx_event_create(0u, &args.event);
    ASSERT_EQ(status, ZX_OK);

    constexpr unsigned kNumWaiters = 4;
    thrd_t waiters[kNumWaiters];
    for (auto& t : waiters) {
        ASSERT_EQ(thrd_create(&t, waiter_thread, &args), thrd_success);
    }

    thrd_t closer;
    ASSERT_EQ(thrd_create(&closer, closer_thread, &args), thrd_success);

    zx_nanosleep(zx_deadline_after(kTestDuration));
    keep_running.store(false);

    for (auto& t : waiters) {
        int res;
        ASSERT_EQ(thrd_join(t, &res), thrd_success);
        ASSERT_EQ(res, ZX_OK);
    }

    int res;
    ASSERT_EQ(thrd_join(closer, &res), thrd_success);
    ASSERT_EQ(res, ZX_OK);

    zx_handle_close(args.event);
    zx_handle_close(args.port->load());

    END_TEST;
}

BEGIN_TEST_CASE(port_tests)
RUN_TEST(basic_test)
RUN_TEST(queue_and_close_test)
RUN_TEST(queue_too_many)
RUN_TEST(async_wait_channel_test)
RUN_TEST(async_wait_event_test_single)
RUN_TEST(async_wait_event_test_repeat)
RUN_TEST(async_wait_invalid_option)
RUN_TEST(async_wait_close_order_1)
RUN_TEST(async_wait_close_order_2)
RUN_TEST(async_wait_close_order_3)
RUN_TEST(async_wait_close_order_4)
RUN_TEST(async_wait_close_order_5)
RUN_TEST(async_wait_close_order_6)
RUN_TEST(channel_pre_writes_test)
RUN_TEST(cancel_event_key)
RUN_TEST(cancel_event_key_after)
RUN_TEST(threads_event)
RUN_TEST_LARGE(cancel_stress)
RUN_TEST_LARGE(signal_close_stress)
RUN_TEST_LARGE(port_close_wait_race_stress)
END_TEST_CASE(port_tests)
