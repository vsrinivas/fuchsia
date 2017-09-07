// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>
#include <stdlib.h>

#include <launchpad/launchpad.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>
#include <unittest/unittest.h>

// This file is for regression tests for race conditions where the test was
// only observed to reproduce the race condition when some scheduling
// tweaks were applied to the software under test -- e.g. adding sleeps or
// sched_yield()/thread_yield() calls, or changing the scheduler to
// randomize its scheduling decisions.

static const char* g_executable_filename;

static void* ThreadFunc(void* thread_arg) {
    mx_process_exit(200);
}

static void Subprocess() {
    pthread_t thread;
    pthread_create(&thread, NULL, ThreadFunc, NULL);
    mx_process_exit(100);
}

// This is a regression test for an issue where the exit status for a
// process -- as reported by mx_object_get_info()'s return_code field --
// could change.  That could happen if multiple threads called
// mx_process_exit() concurrently.
static bool test_process_exit_status_race() {
    BEGIN_TEST;

    // Launch a subprocess.
    launchpad_t* lp;
    ASSERT_EQ(launchpad_create(MX_HANDLE_INVALID, "test_process", &lp),
              MX_OK);
    ASSERT_EQ(launchpad_load_from_file(lp, g_executable_filename), MX_OK);
    const char* args[] = { g_executable_filename, "--subprocess" };
    ASSERT_EQ(launchpad_set_args(lp, fbl::count_of(args), args), MX_OK);
    ASSERT_EQ(launchpad_clone(lp, LP_CLONE_ALL), MX_OK);
    mx_handle_t proc;
    const char* errmsg;
    ASSERT_EQ(launchpad_go(lp, &proc, &errmsg), MX_OK);

    for (;;) {
        // Query the process state.
        mx_info_process_t info1;
        size_t records_read;
        ASSERT_EQ(mx_object_get_info(
                      proc, MX_INFO_PROCESS, &info1, sizeof(info1),
                      &records_read, NULL), MX_OK);
        ASSERT_EQ(records_read, 1u);

        // If the process was reported as exited, query its state again.
        if (info1.exited) {
            EXPECT_TRUE(info1.return_code == 100 ||
                        info1.return_code == 200);

            mx_info_process_t info2;
            ASSERT_EQ(mx_object_get_info(
                          proc, MX_INFO_PROCESS, &info2, sizeof(info2),
                          &records_read, NULL), MX_OK);
            ASSERT_EQ(records_read, 1u);
            // Do the results match what we got before?
            EXPECT_TRUE(info2.exited);
            EXPECT_EQ(info1.return_code, info2.return_code);
            break;
        }
        sched_yield();
    }

    // Clean up.
    ASSERT_EQ(mx_handle_close(proc), MX_OK);

    END_TEST;
}

BEGIN_TEST_CASE(race_tests)
RUN_TEST(test_process_exit_status_race)
END_TEST_CASE(race_tests)

int main(int argc, char** argv) {
    g_executable_filename = argv[0];

    if (argc == 2 && !strcmp(argv[1], "--subprocess")) {
        Subprocess();
        return 0;
    }

    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
