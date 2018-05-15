// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "watchdog.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#include <unittest/unittest.h>

constexpr int WATCHDOG_ERRCODE = 5;

constexpr uint64_t NANOSECONDS_PER_SECOND = 1000 * 1000 * 1000;

// The watchdog thread wakes up after this many seconds to check whether
// a test has timed out. The lower the number this is the more accurate
// the watchdog is with regard to the specified timeout. But there's
// no point in running too frequently. The wait mechanism we use is
// interruptible, so this value can be high and there's no worries of waiting
// for the watchdog to terminate. The watchdog works this way so that we
// can have one watchdog thread that is continuously running instead of
// starting a new watchdog thread for each test. Another goal is to not
// require any synchronization between the watchdog thread and the test.
// E.g., We don't want to have to wait for the watchdog to acknowledge that
// a test is starting and stopping. Instead we just let it run at its own pace.
// Tests often complete in milliseconds, far lower than our "tick".
constexpr int WATCHDOG_TICK_SECONDS = 1;

// Value stored in |active_timeout_seconds| to indicate test is not running.
constexpr int WATCHDOG_TIMEOUT_NOT_RUNNING = INT_MAX;

// This can be overridden by the user by setting env var WATCHDOG_ENV_NAME.
static int base_timeout_seconds = DEFAULT_BASE_TIMEOUT_SECONDS;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// The name of the current test.
// Used to report which test timed out.
static const char* test_name; //TA_GUARDED(mutex)

// The current timeout in effect.
// When tests aren't running we set this to INT_MAX.
static int active_timeout_seconds = WATCHDOG_TIMEOUT_NOT_RUNNING; //TA_GUARDED(mutex)

// The time when the test was started.
// This is the result of clock_gettime converted to nanoseconds.
static uint64_t test_start_time; //TA_GUARDED(mutex)

// True if tests are running.
// Set by watchdog initialize(), reset by watchdog_terminate().
static bool tests_running; //TA_GUARDED(mutex)

static pthread_t watchdog_thread;

// This library is used for both the host and target.
// For portability concerns we use pthread_cond_timedwait to get a
// cancelable wait.
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static uint64_t timespec_to_nanoseconds(const struct timespec* ts) {
    return (ts->tv_sec * NANOSECONDS_PER_SECOND) + ts->tv_nsec;
}

/**
 * Set the base timeout.
 * |timeout| must be >= 0.
 * A value of zero disables the timeout.
 * The timeout must be set before calling watchdog_initialize(), and must
 * not be changed until after watchdog_terminate() is called.
 */
void watchdog_set_base_timeout(int seconds) {
    assert(seconds >= 0);
    base_timeout_seconds = seconds;
}

static int test_timeout_for_type(test_type_t type) {
    int factor;

    switch (type) {
    case TEST_SMALL:
        factor = TEST_TIMEOUT_FACTOR_SMALL;
        break;
    case TEST_MEDIUM:
        factor = TEST_TIMEOUT_FACTOR_MEDIUM;
        break;
    case TEST_LARGE:
        factor = TEST_TIMEOUT_FACTOR_LARGE;
        break;
    case TEST_PERFORMANCE:
        factor = TEST_TIMEOUT_FACTOR_PERFORMANCE;
        break;
    default:
        __UNREACHABLE;
    }

    int64_t timeout = base_timeout_seconds * factor;
    if (timeout > INT_MAX)
        timeout = INT_MAX;
    return static_cast<int>(timeout);
}

/**
 * Return true if watchdog support is enabled for this test run.
 */
bool watchdog_is_enabled() {
    return base_timeout_seconds > 0;
}

static __NO_RETURN void watchdog_signal_timeout(const char* name) {
    unittest_printf_critical("\n\n*** WATCHDOG TIMER FIRED, test: %s ***\n", name);
    exit(WATCHDOG_ERRCODE);
}

static void* watchdog_thread_func(void* arg) {
    pthread_mutex_lock(&mutex);

    for (;;) {
        // Has watchdog_terminate() been called?
        // Test this here, before calling pthread_cond_timedwait(), so that
        // we catch the case of all tests completing and watchdog_terminate
        // being called before we get started. Otherwise we'll wait one tick
        // before we notice this.
        if (!tests_running) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        struct timespec delay;
        clock_gettime(CLOCK_REALTIME, &delay);
        delay.tv_sec += WATCHDOG_TICK_SECONDS;
        // If compiled with #define NDEBUG the assert essentially goes away.
        // Thus we need to protect |result| with __UNUSED lest the compiler
        // complain and fail the build.
        auto result __UNUSED = pthread_cond_timedwait(&cond, &mutex, &delay);
        // We can time-out just as watchdog_terminate() is called, and
        // thus we can't make any assumptions based on |result|.
        assert(result == 0 || result == ETIMEDOUT);

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        uint64_t now_nanos = timespec_to_nanoseconds(&now);
        assert (now_nanos >= test_start_time);
        uint64_t elapsed_nanos = now_nanos - test_start_time;

        // Note: We skip worrying about handling the (rare) case where the
        // test completes but before it can notify us we wake and see that
        // the timeout has been reached.
        uint64_t timeout_nanos = active_timeout_seconds * NANOSECONDS_PER_SECOND;
        if (elapsed_nanos >= timeout_nanos) {
            pthread_mutex_unlock(&mutex);
            watchdog_signal_timeout(test_name);
            /* NOTREACHED */
        }
    }

    return nullptr;
}

/**
 * Start the watchdog thread.
 *
 * The thread begins in an idle state, waiting for watchdog_start().
 * This must only be called once.
 */
void watchdog_initialize() {
    if (watchdog_is_enabled()) {
        tests_running = true;
        int res = pthread_create(&watchdog_thread, NULL, &watchdog_thread_func, NULL);
        if (res != 0) {
            unittest_printf_critical("ERROR STARTING WATCHDOG THREAD: %d(%s)\n", res, strerror(res));
            exit(WATCHDOG_ERRCODE);
        }
    }
}

/**
 * Turn on the watchdog timer for test |name|.
 *
 * Storage for |name| must survive the duration of the test.
 *
 * If the timer goes off the process terminates.
 * This must be called at the start of a test.
 */
void watchdog_start(test_type_t type, const char* name) {
    if (watchdog_is_enabled()) {
        pthread_mutex_lock(&mutex);
        test_name = name;
        active_timeout_seconds = test_timeout_for_type(type);
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        test_start_time = timespec_to_nanoseconds(&now);
        pthread_mutex_unlock(&mutex);
    }
}

/**
 * Call this to turn off the watchdog timer.
 *
 * Yeah, there's a "race" if a test finishes right when we're called.
 * We don't worry about this small window given the amount of time we wait.
 * This must be called after watchdog_start().
 */
void watchdog_cancel() {
    if (watchdog_is_enabled()) {
        pthread_mutex_lock(&mutex);
        active_timeout_seconds = WATCHDOG_TIMEOUT_NOT_RUNNING;
        test_name = nullptr;
        pthread_mutex_unlock(&mutex);
    }
}

/**
 * Terminate the watchdog thread.
 *
 * This must be called after all tests complete.
 */
void watchdog_terminate() {
    // All tests must have completed.
    assert(active_timeout_seconds == WATCHDOG_TIMEOUT_NOT_RUNNING);

    if (watchdog_is_enabled()) {
        pthread_mutex_lock(&mutex);
        tests_running = false;
        int res = pthread_cond_signal(&cond);
        assert(res == 0);
        pthread_mutex_unlock(&mutex);
        res = pthread_join(watchdog_thread, NULL);
        assert(res == 0);
    }
}
