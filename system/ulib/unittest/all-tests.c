// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

static struct test_case_element* test_case_list = NULL;
static struct test_case_element* failed_test_case_list = NULL;

/*
 * Registers a test case with the unit test framework.
 */
void unittest_register_test_case(struct test_case_element* elem) {
    elem->next = test_case_list;
    test_case_list = elem;
}

/*
 * Runs all registered test cases.
 */
bool unittest_run_all_tests(int argc, char** argv) {
    unsigned int n_tests = 0;
    unsigned int n_success = 0;
    unsigned int n_failed = 0;

    int prev_verbosity_level = -1;
    if (argc == 2) {
        if ((strlen(argv[1]) == 3) && (argv[1][0] == 'v') && (argv[1][1] = '=')) {
            prev_verbosity_level = unittest_set_verbosity_level(argv[1][2] - '0');
        }
    }

    bool all_success = true;
    struct test_case_element* current = test_case_list;
    while (current) {
        if (!current->test_case()) {
            current->failed_next = failed_test_case_list;
            failed_test_case_list = current;
            all_success = false;
        }
        current = current->next;
        n_tests++;
    }

    if (prev_verbosity_level >= 0)
        unittest_set_verbosity_level(prev_verbosity_level);

    if (all_success) {
        n_success = n_tests;
        unittest_printf_critical("SUCCESS!  All test cases passed!\n");
    } else {
        struct test_case_element* failed = failed_test_case_list;
        while (failed) {
            struct test_case_element* failed_next =
                failed->failed_next;
            failed->failed_next = NULL;
            failed = failed_next;
            n_failed++;
        }
        n_success = n_tests - n_failed;
        failed_test_case_list = NULL;
    }

    unittest_printf_critical("\n====================================================\n");
    unittest_printf_critical("    CASES:  %d     SUCCESS:  %d     FAILED:  %d   ",
                             n_tests, n_success, n_failed);
    unittest_printf_critical("\n====================================================\n");

    return all_success;
}
