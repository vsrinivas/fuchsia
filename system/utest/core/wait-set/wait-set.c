// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>
#include <threads.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <unittest/unittest.h>

bool wait_set_create_test(void) {
    BEGIN_TEST;

    mx_handle_t ws;
    ASSERT_EQ(mx_waitset_create(0, &ws), NO_ERROR, "");
    ASSERT_GT(ws, 0, "mx_waitset_create() failed");

    mx_size_t sz;
    mx_info_handle_basic_t ws_info;
    mx_status_t status = mx_object_get_info(ws, MX_INFO_HANDLE_BASIC, sizeof(ws_info.rec), &ws_info, sizeof(ws_info), &sz);
    ASSERT_EQ(status, NO_ERROR, "");
    ASSERT_EQ(sz, sizeof(ws_info), "");
    EXPECT_EQ(ws_info.rec.rights, MX_RIGHT_READ | MX_RIGHT_WRITE, "");
    EXPECT_EQ(ws_info.rec.type, (uint32_t)MX_OBJ_TYPE_WAIT_SET, "");

    EXPECT_EQ(mx_handle_close(ws), NO_ERROR, "");

    END_TEST;
}

bool wait_set_add_remove_test(void) {
    BEGIN_TEST;

    mx_handle_t ev[3];
    ASSERT_EQ(mx_event_create(0u, &ev[0]), 0, "mx_event_create() failed");
    ASSERT_EQ(mx_event_create(0u, &ev[1]), 0, "mx_event_create() failed");
    ASSERT_EQ(mx_event_create(0u, &ev[2]), 0, "mx_event_create() failed");

    mx_handle_t ws;
    ASSERT_EQ(mx_waitset_create(0, &ws), NO_ERROR, "");
    ASSERT_GT(ws, 0, "mx_waitset_create() failed");

    const uint64_t cookie1 = 0u;
    ASSERT_EQ(mx_waitset_add(ws, cookie1, ev[0], MX_USER_SIGNAL_0), NO_ERROR, "");

    const uint64_t cookie2 = (uint64_t)-1;
    ASSERT_EQ(mx_waitset_add(ws, cookie2, ev[1], MX_USER_SIGNAL_1), NO_ERROR, "");

    // Can add a handle that's already in there.
    const uint64_t cookie3 = 12345678901234567890ull;
    ASSERT_EQ(mx_waitset_add(ws, cookie3, ev[0], MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1), NO_ERROR,
              "");

    // Remove |cookie1|.
    ASSERT_EQ(mx_waitset_remove(ws, cookie1), NO_ERROR, "");

    // Now can reuse |cookie1|.
    ASSERT_EQ(mx_waitset_add(ws, cookie1, ev[2], MX_USER_SIGNAL_0), NO_ERROR, "");

    // Can close a handle (|ev[1]|) that's in a wait set.
    EXPECT_EQ(mx_handle_close(ev[1]), NO_ERROR, "");

    // And then remove it (|cookie2|).
    ASSERT_EQ(mx_waitset_remove(ws, cookie2), NO_ERROR, "");

    // Close |ev[2]| also.
    EXPECT_EQ(mx_handle_close(ev[2]), NO_ERROR, "");

    // Now close the wait set; it has an entry with a close handle (|cookie1|) and one with an open
    // handle (|cookie3|).
    EXPECT_EQ(mx_handle_close(ws), NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(ev[0]), NO_ERROR, "");

    END_TEST;
}

bool wait_set_bad_add_remove_test(void) {
    BEGIN_TEST;

    mx_handle_t ev;
    ASSERT_EQ(mx_event_create(0u, &ev), 0, "mx_event_create() failed");

    mx_handle_t ws;
    ASSERT_EQ(mx_waitset_create(0, &ws), NO_ERROR, "");
    ASSERT_GT(ws, 0, "mx_waitset_create() failed");

    const uint64_t cookie1 = 123u;
    EXPECT_EQ(mx_waitset_add(MX_HANDLE_INVALID, cookie1, ev, MX_USER_SIGNAL_0), ERR_BAD_HANDLE,
              "");
    EXPECT_EQ(mx_waitset_add(ws, cookie1, MX_HANDLE_INVALID, MX_USER_SIGNAL_0), ERR_BAD_HANDLE,
              "");

    EXPECT_EQ(mx_waitset_remove(MX_HANDLE_INVALID, cookie1), ERR_BAD_HANDLE, "");
    EXPECT_EQ(mx_waitset_remove(ws, cookie1), ERR_NOT_FOUND, "");

    EXPECT_EQ(mx_waitset_add(ws, cookie1, ev, MX_USER_SIGNAL_0), NO_ERROR, "");
    EXPECT_EQ(mx_waitset_add(ws, cookie1, ev, MX_USER_SIGNAL_0), ERR_ALREADY_EXISTS, "");

    const uint64_t cookie2 = 456u;
    EXPECT_EQ(mx_waitset_remove(ws, cookie2), ERR_NOT_FOUND, "");

    EXPECT_EQ(mx_waitset_remove(ws, cookie1), NO_ERROR, "");
    EXPECT_EQ(mx_waitset_remove(ws, cookie1), ERR_NOT_FOUND, "");

    // Wait sets aren't waitable.
    EXPECT_EQ(mx_waitset_add(ws, cookie2, ws, 0u), ERR_NOT_SUPPORTED, "");

    // TODO(vtl): Test that both handles are properly tested for rights.

    EXPECT_EQ(mx_handle_close(ws), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(ev), NO_ERROR, "");

    END_TEST;
}

// Checks that |results[0...num_results-1]| contains exactly one result that matches the other
// parameters (if a result matches |cookie|, the other fields must match as well).
static bool check_results(uint32_t num_results,
                          mx_waitset_result_t* results,
                          uint64_t cookie,
                          mx_status_t wait_result,
                          mx_signals_t satisfied) {
    uint32_t i = 0u;
    for (; i < num_results; i++) {
        if (results[i].cookie == cookie)
            break;
    }
    if (i == num_results)
        return false;  // Not found.

    if (results[i].status != wait_result)
        return false;
    if (results[i].observed != satisfied)
        return false;

    // Check that none of the remaining entries has the same cookie.
    for (i++; i < num_results; i++) {
        if (results[i].cookie == cookie)
            return false;
    }

    return true;
}

bool wait_set_wait_single_thread_1_test(void) {
    BEGIN_TEST;

    mx_handle_t ev[3];
    ASSERT_EQ(mx_event_create(0u, &ev[0]), 0, "mx_event_create() failed");
    ASSERT_EQ(mx_event_create(0u, &ev[1]), 0, "mx_event_create() failed");
    ASSERT_EQ(mx_event_create(0u, &ev[2]), 0, "mx_event_create() failed");

    mx_handle_t ws;
    ASSERT_EQ(mx_waitset_create(0, &ws), NO_ERROR, "");
    ASSERT_GT(ws, 0, "mx_waitset_create() failed");

    mx_waitset_result_t results[10] = {};
    uint32_t num_results = 5u;
    EXPECT_EQ(mx_waitset_wait(ws, 0u, results, &num_results), ERR_TIMED_OUT, "");
    // It should leave |num_results| and |max_results| alone on error.
    EXPECT_EQ(num_results, 5u, "mx_waitset_wait() modified num_results");

    num_results = 5u;
    // Nonzero timeout and null |max_results| argument.
    EXPECT_EQ(mx_waitset_wait(ws, 5u, results, &num_results), ERR_TIMED_OUT, "");
    // It should leave |num_results| alone on error.
    EXPECT_EQ(num_results, 5u, "mx_waitset_wait() modified num_results");

    const uint64_t cookie0 = 1u;
    EXPECT_EQ(mx_waitset_add(ws, cookie0, ev[0], MX_USER_SIGNAL_0), NO_ERROR, "");
    const uint64_t cookie1a = 2u;
    EXPECT_EQ(mx_waitset_add(ws, cookie1a, ev[1], MX_USER_SIGNAL_0), NO_ERROR, "");
    const uint64_t cookie2 = 3u;
    EXPECT_EQ(mx_waitset_add(ws, cookie2, ev[2], MX_USER_SIGNAL_0), NO_ERROR, "");
    const uint64_t cookie1b = 4u;
    EXPECT_EQ(mx_waitset_add(ws, cookie1b, ev[1], MX_USER_SIGNAL_0), NO_ERROR, "");

    num_results = 5u;
    // Nothing signaled; should still time out.
    EXPECT_EQ(mx_waitset_wait(ws, 0u, results, &num_results), ERR_TIMED_OUT, "");
    // It should leave |num_results| and |max_results| alone on error.
    EXPECT_EQ(num_results, 5u, "mx_waitset_wait() modified num_results");

    ASSERT_EQ(mx_object_signal(ev[0], 0u, MX_USER_SIGNAL_0), NO_ERROR, "");
    num_results = 5u;
    ASSERT_EQ(mx_waitset_wait(ws, 0u, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 1u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie0, NO_ERROR, MX_USER_SIGNAL_0), "");

    ASSERT_EQ(mx_object_signal(ev[1], 0u, MX_USER_SIGNAL_0), NO_ERROR, "");
    num_results = 5u;
    ASSERT_EQ(mx_waitset_wait(ws, 10u, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 3u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie0, NO_ERROR, MX_USER_SIGNAL_0), "");
    EXPECT_TRUE(check_results(num_results, results, cookie1a, NO_ERROR, MX_USER_SIGNAL_0), "");
    EXPECT_TRUE(check_results(num_results, results, cookie1b, NO_ERROR, MX_USER_SIGNAL_0), "");

    num_results = 2u;
    ASSERT_EQ(mx_waitset_wait(ws, MX_TIME_INFINITE, results, &num_results), NO_ERROR,
                               "");
    ASSERT_EQ(num_results, 2u, "wrong num_results from mx_waitset_wait()");
    unsigned found = check_results(num_results, results, cookie0, NO_ERROR, MX_USER_SIGNAL_0);
    found += check_results(num_results, results, cookie1a, NO_ERROR, MX_USER_SIGNAL_0);
    found += check_results(num_results, results, cookie1b, NO_ERROR, MX_USER_SIGNAL_0);
    EXPECT_EQ(found, 2u, "");

    // Can pass null for |results| if |num_results| is zero.
    num_results = 0u;
    ASSERT_EQ(mx_waitset_wait(ws, MX_TIME_INFINITE, NULL, &num_results), NO_ERROR,
                               "");
    ASSERT_EQ(num_results, 0u, "wrong num_results from mx_waitset_wait()");

    EXPECT_EQ(mx_handle_close(ev[2]), NO_ERROR, "");
    num_results = 10u;
    ASSERT_EQ(mx_waitset_wait(ws, MX_TIME_INFINITE, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 4u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie0, NO_ERROR, MX_USER_SIGNAL_0), "");
    EXPECT_TRUE(check_results(num_results, results, cookie1a, NO_ERROR, MX_USER_SIGNAL_0), "");
    EXPECT_TRUE(check_results(num_results, results, cookie1b, NO_ERROR, MX_USER_SIGNAL_0), "");
    EXPECT_TRUE(check_results(num_results, results, cookie2, ERR_HANDLE_CLOSED, 0u), "");

    ASSERT_EQ(mx_waitset_remove(ws, cookie1b), NO_ERROR, "");
    num_results = 10u;
    ASSERT_EQ(mx_waitset_wait(ws, 0u, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 3u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie0, NO_ERROR, MX_USER_SIGNAL_0), "");
    EXPECT_TRUE(check_results(num_results, results, cookie1a, NO_ERROR, MX_USER_SIGNAL_0), "");
    EXPECT_TRUE(check_results(num_results, results, cookie2, ERR_HANDLE_CLOSED, 0u), "");

    // Check that it handles going from satisfied to unsatisfied (but satisfiable and not canceled)
    // properly.
    ASSERT_EQ(mx_object_signal(ev[0], MX_USER_SIGNAL_0, 0u), NO_ERROR, "");
    num_results = 10u;
    ASSERT_EQ(mx_waitset_wait(ws, 0u, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 2u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie1a, NO_ERROR, MX_USER_SIGNAL_0), "");
    EXPECT_TRUE(check_results(num_results, results, cookie2, ERR_HANDLE_CLOSED, 0u), "");

    EXPECT_EQ(mx_handle_close(ws), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(ev[0]), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(ev[1]), NO_ERROR, "");

    END_TEST;
}

bool wait_set_wait_single_thread_2_test(void) {
    BEGIN_TEST;

    // Need something for which can can provoke unsatisfiability.
    mx_handle_t mp[2] = {};
    ASSERT_EQ(mx_channel_create(0u, mp, mp + 1), NO_ERROR, "");
    ASSERT_GT(mp[0], 0, "mx_channel_create() failed");
    ASSERT_GT(mp[1], 0, "mx_channel_create() failed");

    mx_handle_t ws;
    ASSERT_EQ(mx_waitset_create(0, &ws), NO_ERROR, "");
    ASSERT_GT(ws, 0, "mx_waitset_create() failed");

    const uint64_t cookie1 = 987654321098765ull;
    EXPECT_EQ(mx_waitset_add(ws, cookie1, mp[0], MX_SIGNAL_READABLE), NO_ERROR, "");
    const uint64_t cookie2 = 789023457890412ull;
    EXPECT_EQ(mx_waitset_add(ws, cookie2, mp[0], MX_SIGNAL_PEER_CLOSED), NO_ERROR, "");

    mx_waitset_result_t results[5] = {};
    uint32_t num_results = 5u;
    EXPECT_EQ(mx_waitset_wait(ws, 0u, results, &num_results), ERR_TIMED_OUT, "");
    EXPECT_EQ(num_results, 5u, "mx_waitset_wait() modified num_results");

    EXPECT_EQ(mx_handle_close(mp[1]), NO_ERROR, "");
    num_results = 5u;
    EXPECT_EQ(mx_waitset_wait(ws, 0u, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 1u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie2, NO_ERROR, MX_SIGNAL_PEER_CLOSED), "");

    EXPECT_EQ(mx_handle_close(mp[0]), NO_ERROR, "");
    num_results = 5u;
    EXPECT_EQ(mx_waitset_wait(ws, MX_TIME_INFINITE, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 2u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie1, ERR_HANDLE_CLOSED, 0u), "");
    EXPECT_TRUE(check_results(num_results, results, cookie2, ERR_HANDLE_CLOSED, 0u), "");

    EXPECT_EQ(mx_handle_close(ws), NO_ERROR, "");

    END_TEST;
}

static int signaler_thread_fn(void* arg) {
    assert(arg);
    mx_handle_t ev = *(mx_handle_t*)arg;
    assert(ev > 0);
    mx_nanosleep(MX_MSEC(200));
    mx_status_t status = mx_object_signal(ev, 0u, MX_USER_SIGNAL_0);
    assert(status == NO_ERROR);
    return 0;
}

static int closer_thread_fn(void* arg) {
    assert(arg);
    mx_handle_t h = *(mx_handle_t*)arg;
    assert(h > 0);
    mx_nanosleep(MX_MSEC(200));
    mx_status_t status = mx_handle_close(h);
    assert(status == NO_ERROR);
    return 0;
}

bool wait_set_wait_threaded_test(void) {
    BEGIN_TEST;

    mx_handle_t ev;
    ASSERT_EQ(mx_event_create(0u, &ev), 0, "mx_event_create() failed");

    mx_handle_t ws;
    ASSERT_EQ(mx_waitset_create(0, &ws), NO_ERROR, "");
    ASSERT_GT(ws, 0, "mx_waitset_create() failed");

    const uint64_t cookie = 123u;
    EXPECT_EQ(mx_waitset_add(ws, cookie, ev, MX_USER_SIGNAL_0), NO_ERROR, "");

    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, signaler_thread_fn, &ev), thrd_success, "thrd_create() failed");

    mx_waitset_result_t results[5] = {};
    uint32_t num_results = 5u;
    EXPECT_EQ(mx_waitset_wait(ws, MX_TIME_INFINITE, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 1u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie, NO_ERROR, MX_USER_SIGNAL_0), "");

    // Join.
    ASSERT_EQ(thrd_join(thread, NULL), thrd_success, "");

    ASSERT_EQ(mx_object_signal(ev, MX_USER_SIGNAL_0, 0u), NO_ERROR, "");

    ASSERT_EQ(thrd_create(&thread, closer_thread_fn, &ev), thrd_success, "thrd_create() failed");

    num_results = 5u;
    EXPECT_EQ(mx_waitset_wait(ws, MX_TIME_INFINITE, results, &num_results), NO_ERROR, "");
    ASSERT_EQ(num_results, 1u, "wrong num_results from mx_waitset_wait()");
    EXPECT_TRUE(check_results(num_results, results, cookie, ERR_HANDLE_CLOSED, 0u), "");

    // Join.
    ASSERT_EQ(thrd_join(thread, NULL), thrd_success, "");

    EXPECT_EQ(mx_handle_close(ws), NO_ERROR, "");

    END_TEST;
}

bool wait_set_wait_cancelled_test(void) {
    BEGIN_TEST;

    mx_handle_t ev;
    ASSERT_EQ(mx_event_create(0u, &ev), 0, "mx_event_create() failed");

    mx_handle_t ws;
    ASSERT_EQ(mx_waitset_create(0, &ws), NO_ERROR, "");
    ASSERT_GT(ws, 0, "mx_waitset_create() failed");

    const uint64_t cookie = 123u;
    EXPECT_EQ(mx_waitset_add(ws, cookie, ev, MX_USER_SIGNAL_0), NO_ERROR, "");

    // We close the wait set handle!
    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, closer_thread_fn, &ws), thrd_success, "thrd_create() failed");

    mx_waitset_result_t results[5] = {};
    uint32_t num_results = 5u;
    // There's actually a race here; we could actually get ERR_BAD_HANDLE if we don't start the wait
    // before the thread closes |ws|. But let's hope the thread's sleep is long enough.
    EXPECT_EQ(mx_waitset_wait(ws, MX_TIME_INFINITE, results, &num_results), ERR_HANDLE_CLOSED,
              "");
    EXPECT_EQ(num_results, 5u, "mx_waitset_wait() modified num_results");

    // Join.
    ASSERT_EQ(thrd_join(thread, NULL), thrd_success, "");

    EXPECT_EQ(mx_handle_close(ev), NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(wait_set_tests)
RUN_TEST(wait_set_create_test)
RUN_TEST(wait_set_add_remove_test)
RUN_TEST(wait_set_bad_add_remove_test)
RUN_TEST(wait_set_wait_single_thread_1_test)
RUN_TEST(wait_set_wait_single_thread_2_test)
RUN_TEST(wait_set_wait_threaded_test)
RUN_TEST(wait_set_wait_cancelled_test)
END_TEST_CASE(wait_set_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
