// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unittest/unittest.h>

// How many times to try a given window size.
#define NUM_PASSES_PER_WINDOW 100

// qsort comparison function for mx_handle_t.
static int handle_cmp(const void* left, const void* right) {
    return *(const mx_handle_t*)left - *(const mx_handle_t*)right;
}

// Prints a message and exits the process with a non-zero status.
// This will stop any further tests in this file from running.
#define FATALF(str, x...)                                    \
    do {                                                     \
        unittest_printf_critical(                            \
            "\nFATAL:%s:%d: " str, __FILE__, __LINE__, ##x); \
        exit(-2);                                            \
    } while (false)

// Creates/closes |window_size| handles as quickly as possible and looks
// for aliases. Returns true if any aliases were found.
static bool find_handle_value_aliases(const size_t window_size) {
    mx_handle_t event;
    mx_status_t s = mx_event_create(0, &event);
    if (s != MX_OK) {
        FATALF("Can't create event: %s\n", mx_status_get_string(s));
    }
    mx_handle_t* handle_log =
        (mx_handle_t*)malloc(window_size * sizeof(mx_handle_t));

    bool saw_aliases = false;
    int pass = 0;
    while (pass++ < NUM_PASSES_PER_WINDOW && !saw_aliases) {
        // Create and close a bunch of handles as quickly as possible.
        memset(handle_log, 0, window_size * sizeof(mx_handle_t));
        for (size_t i = 0; i < window_size; i++) {
            s = mx_handle_duplicate(event, MX_RIGHT_SAME_RIGHTS, &handle_log[i]);
            if (s != MX_OK) {
                FATALF("[i == %zd] Can't duplicate event: %s\n",
                       i, mx_status_get_string(s));
            }
            if (handle_log[i] <= 0) {
                FATALF("[i == %zd] Got bad handle %d\n", i, handle_log[i]);
            }
            s = mx_handle_close(handle_log[i]);
            if (s != MX_OK) {
                FATALF("[i == %zd] Can't close handle %d: %s\n",
                       i, handle_log[i], mx_status_get_string(s));
            }
        }

        // Look for any aliases.
        qsort(handle_log, window_size, sizeof(mx_handle_t), handle_cmp);
        for (size_t i = 1; i < window_size; i++) {
            if (handle_log[i] == handle_log[i - 1]) {
                saw_aliases = true;
                break;
            }
        }
    }

    free(handle_log);
    mx_handle_close(event);
    return saw_aliases;
}

// Searches for the largest window size that doesn't contain
// handle value aliases.
static size_t find_handle_alias_window_size(void) {
    size_t min_fail = 8192; // "fail" meaning "aliases found"
    size_t max_pass = 1;    // "pass" meaning "no aliases found"
    while (true) {
        size_t cur_win = (min_fail - 1 + max_pass + 1) / 2;
        if (cur_win <= max_pass) {
            return max_pass;
        }
        unittest_printf("    window_size %4zd: ", cur_win);
        fflush(stdout);
        if (find_handle_value_aliases(cur_win)) {
            unittest_printf("ALIAS FOUND\n");
            min_fail = cur_win;
        } else {
            unittest_printf("no alias found\n");
            max_pass = cur_win;
        }
    }
}

// This test isn't deterministic, because its behavior depends on the
// system-wide usage of the kernel's handle arena.
// It can produce a false failure if someone else consumes/recycles handle
// slots in the same way this test does.
// It can produce a false success if someone else consumes and holds onto
// handle slots, so that this test never gets a chance to see the same
// slot each time.
static bool handle_value_alias_test(void) {
    BEGIN_TEST;
    unittest_printf("\n");
    size_t window_size = find_handle_alias_window_size();
    unittest_printf(
        "    Converged at %zd (largest window_size with no aliases)\n",
        window_size);

    // The handle table as of 13 Mar 2017 should let us re-use a handle
    // slot 4096 times before producing an alias. Use half that as our
    // target to bias the test away from false failures.
    const size_t min_window_size = 2048;
    EXPECT_GE(window_size, min_window_size, "");
    END_TEST;
}

BEGIN_TEST_CASE(handle_reuse)
RUN_TEST_LARGE(handle_value_alias_test); // Potentially flaky => large test
END_TEST_CASE(handle_reuse)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
