// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <threads.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <fbl/algorithm.h>

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

    status = zx_port_queue(port, nullptr, 1u);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);

    status = zx_port_queue(port, &in, 1u);
    EXPECT_EQ(status, ZX_OK);

    status = zx_port_wait(port, ZX_TIME_INFINITE, &out, 1u);
    EXPECT_EQ(status, ZX_OK);

    EXPECT_EQ(out.key, 12u);
    EXPECT_EQ(out.type, ZX_PKT_TYPE_USER);
    EXPECT_EQ(out.status, -3);

    EXPECT_EQ(memcmp(&in.user, &out.user, sizeof(zx_port_packet_t::user)), 0);

    status = zx_handle_close(port);
    EXPECT_EQ(status, ZX_OK);

    END_TEST;
}

template <size_t Count>
static bool queue_count_valid_test() {
    BEGIN_TEST;

    zx_handle_t port;
    zx_status_t status = zx_port_create(0u, &port);
    EXPECT_EQ(status, ZX_OK);

    // This test relies on only 0 or 1 being a valid count. This might
    // eventually change. For now, we can stack allocate 1 packet and
    // know it is sufficient for all instantiations of this test.
    static_assert(Count <= 1, "");
    const zx_port_packet_t in = {
    };
    status = zx_port_queue(port, &in, Count);
    EXPECT_EQ(status, ZX_OK);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);

    END_TEST;
}

template <size_t Count>
static bool queue_count_invalid_test() {
    BEGIN_TEST;

    zx_handle_t port;
    zx_status_t status = zx_port_create(0u, &port);
    EXPECT_EQ(status, ZX_OK);

    const zx_port_packet_t in[Count] = {
    };
    status = zx_port_queue(port, in, Count);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);

    END_TEST;
}

template <size_t Count>
static bool wait_count_valid_test() {
    BEGIN_TEST;

    zx_handle_t port;
    zx_status_t status = zx_port_create(0u, &port);
    EXPECT_EQ(status, ZX_OK);

    const zx_port_packet_t in = {
    };
    status = zx_port_queue(port, &in, 1u);
    EXPECT_EQ(status, ZX_OK);

    // This test relies on only 0 or 1 being a valid count. This might
    // eventually change. For now, we can stack allocate 1 packet and
    // know it is sufficient for all instantiations of this test.
    static_assert(Count <= 1, "");
    zx_port_packet_t out = {
    };
    status = zx_port_wait(port, ZX_TIME_INFINITE, &out, Count);
    EXPECT_EQ(status, ZX_OK);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);

    END_TEST;
}

template <size_t Count>
static bool wait_count_invalid_test() {
    BEGIN_TEST;

    zx_handle_t port;
    zx_status_t status = zx_port_create(0u, &port);
    EXPECT_EQ(status, ZX_OK);

    const zx_port_packet_t in = {
    };
    status = zx_port_queue(port, &in, 1u);
    EXPECT_EQ(status, ZX_OK);

    zx_port_packet_t out[Count] = {
    };
    status = zx_port_wait(port, ZX_TIME_INFINITE, out, Count);
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);

    END_TEST;
}

static bool queue_and_close_test(void) {
    BEGIN_TEST;
    zx_status_t status;

    zx_handle_t port;
    status = zx_port_create(0, &port);
    EXPECT_EQ(status, ZX_OK, "could not create port");

    zx_port_packet_t out0 = {};
    status = zx_port_wait(port, zx_deadline_after(ZX_USEC(1)), &out0, 1u);
    EXPECT_EQ(status, ZX_ERR_TIMED_OUT);

    const zx_port_packet_t in = {
        1ull,
        ZX_PKT_TYPE_USER,
        0,
        { {} }
    };

    status = zx_port_queue(port, &in, 1u);
    EXPECT_EQ(status, ZX_OK);

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

        status = zx_port_wait(port, zx_deadline_after(ZX_USEC(200)), &out, 1u);
        EXPECT_EQ(status, ZX_ERR_TIMED_OUT);

        status = zx_channel_write(ch[0], 0u, "here", 4, nullptr, 0u);
        EXPECT_EQ(status, ZX_OK);

        status = zx_port_wait(port, ZX_TIME_INFINITE, &out, 1u);
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

    status = zx_port_wait(port, zx_deadline_after(ZX_USEC(200)), &out1, 1u);
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
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE) &&
           async_wait_close_order(order, ZX_WAIT_ASYNC_REPEATING);
}

static bool async_wait_close_order_2() {
    int order[] = {0, 2, 1};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE) &&
           async_wait_close_order(order, ZX_WAIT_ASYNC_REPEATING);
}

static bool async_wait_close_order_3() {
    int order[] = {1, 2, 0};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE) &&
           async_wait_close_order(order, ZX_WAIT_ASYNC_REPEATING);
}

static bool async_wait_close_order_4() {
    int order[] = {1, 0, 2};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE) &&
           async_wait_close_order(order, ZX_WAIT_ASYNC_REPEATING);
}

static bool async_wait_close_order_5() {
    int order[] = {2, 1, 0};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE) &&
           async_wait_close_order(order, ZX_WAIT_ASYNC_REPEATING);
}

static bool async_wait_close_order_6() {
    int order[] = {2, 0, 1};
    return async_wait_close_order(order, ZX_WAIT_ASYNC_ONCE) &&
           async_wait_close_order(order, ZX_WAIT_ASYNC_REPEATING);
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
        status = zx_port_wait(port, ZX_TIME_INFINITE, &out, 1u);
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

    status = zx_object_wait_async(ev, port, key0,
        ZX_EVENT_SIGNALED | ZX_USER_SIGNAL_2, ZX_WAIT_ASYNC_REPEATING);
    EXPECT_EQ(status, ZX_OK);

    zx_port_packet_t out = {};
    uint64_t count[3] = {};

    for (int ix = 0; ix != 24; ++ix) {
        uint32_t ub = (ix % 2) ? 0u : ZX_USER_SIGNAL_2;
        EXPECT_EQ(zx_object_signal(ev, 0u, ZX_EVENT_SIGNALED | ub), ZX_OK);
        EXPECT_EQ(zx_object_signal(ev, ZX_EVENT_SIGNALED | ub, 0u), ZX_OK);

        ASSERT_EQ(zx_port_wait(port, 0ull, &out, 1u), ZX_OK);
        ASSERT_EQ(out.type, ZX_PKT_TYPE_SIGNAL_REP);
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
    const uint32_t kInvalidOption = ZX_WAIT_ASYNC_REPEATING + 1;
    EXPECT_EQ(zx_object_wait_async(event, port, kKey, ZX_EVENT_SIGNALED,
                                   kInvalidOption), ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx_handle_close(event), ZX_OK);
    ASSERT_EQ(zx_handle_close(port), ZX_OK);
    END_TEST;
}

static bool pre_writes_channel_test(uint32_t mode) {
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
        ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, mode);
    EXPECT_EQ(status, ZX_OK);

    zx_port_packet_t out = {};
    int wait_count = 0;
    uint64_t read_count = 0u;

    while (true) {
        status = zx_port_wait(port, 0ull, &out, 1u);
        if (status != ZX_OK)
            break;
        wait_count++;
        if (out.signal.trigger != ZX_CHANNEL_PEER_CLOSED)
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

static bool channel_pre_writes_once() {
    return pre_writes_channel_test(ZX_WAIT_ASYNC_ONCE);
}

static bool channel_pre_writes_repeat() {
    return pre_writes_channel_test(ZX_WAIT_ASYNC_REPEATING);
}

static bool cancel_event(uint32_t wait_mode) {
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
            ev, port, keys[ix], ZX_EVENT_SIGNALED, wait_mode), ZX_OK);
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
        status = zx_port_wait(port, 0ull, &out, 1u);
        if (status != ZX_OK)
            break;
        wait_count++;
        key_sum += out.key;
        EXPECT_EQ(out.signal.trigger, ZX_EVENT_SIGNALED);
        EXPECT_EQ(out.signal.observed, ZX_EVENT_SIGNALED);
    }

    if (wait_mode == ZX_WAIT_ASYNC_ONCE) {
        // We cancel after the packet has been delivered.
        EXPECT_EQ(zx_port_cancel(port, ev, 128u), ZX_ERR_NOT_FOUND);
    }

    EXPECT_EQ(wait_count, 2);
    EXPECT_EQ(key_sum, keys[0] + keys[2]);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);
    EXPECT_EQ(zx_handle_close(ev), ZX_OK);
    END_TEST;
}

static bool cancel_event_key_once() {
    return cancel_event(ZX_WAIT_ASYNC_ONCE);
}

static bool cancel_event_key_repeat() {
    return cancel_event(ZX_WAIT_ASYNC_REPEATING);
}

static bool cancel_event_after(uint32_t wait_mode) {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t port;

    EXPECT_EQ(zx_port_create(0, &port), ZX_OK);

    zx_handle_t ev[3];
    const uint64_t keys[] = {128u, 3u, 3u};

    for (uint32_t ix = 0; ix != fbl::count_of(keys); ++ix) {

        EXPECT_EQ(zx_event_create(0u, &ev[ix]), ZX_OK);
        EXPECT_EQ(zx_object_wait_async(
            ev[ix], port, keys[ix], ZX_EVENT_SIGNALED, wait_mode), ZX_OK);
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
        status = zx_port_wait(port, 0ull, &out, 1u);
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

static bool cancel_event_key_once_after() {
    return cancel_event_after(ZX_WAIT_ASYNC_ONCE);
}

static bool cancel_event_key_repeat_after() {
    return cancel_event_after(ZX_WAIT_ASYNC_REPEATING);
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
        auto st = zx_port_wait(ctx->port, ZX_TIME_INFINITE, &out, 1u);
        if (st < 0)
            return st;
        ++received;
    } while (received < ctx->count);
    return 0;
}

static bool threads_event(uint32_t wait_mode) {
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
                  ev, port, (500u + ix), ZX_EVENT_SIGNALED, wait_mode), ZX_OK);
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

static bool threads_event_once() {
    return threads_event(ZX_WAIT_ASYNC_ONCE);
}

static bool threads_event_repeat() {
    return threads_event(ZX_WAIT_ASYNC_REPEATING);
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
    // removing itself from the event and the cancelation logic which is
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

BEGIN_TEST_CASE(port_tests)
RUN_TEST(basic_test)
RUN_TEST(queue_count_valid_test<1u>)
RUN_TEST(queue_count_invalid_test<2u>)
RUN_TEST(queue_count_invalid_test<23u>)
RUN_TEST(wait_count_valid_test<1u>)
RUN_TEST(wait_count_invalid_test<2u>)
RUN_TEST(wait_count_invalid_test<23u>)
RUN_TEST(queue_and_close_test)
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
RUN_TEST(channel_pre_writes_once)
RUN_TEST(channel_pre_writes_repeat)
RUN_TEST(cancel_event_key_once)
RUN_TEST(cancel_event_key_repeat)
RUN_TEST(cancel_event_key_once_after)
RUN_TEST(cancel_event_key_repeat_after)
RUN_TEST(threads_event_once)
RUN_TEST(threads_event_repeat)
RUN_TEST_LARGE(cancel_stress)
END_TEST_CASE(port_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
