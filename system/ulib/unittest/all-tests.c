// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
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

bool unittest_run_all_tests_etc(test_type_t type, struct test_result* result) {
    unsigned int n_tests = 0;
    unsigned int n_success = 0;
    unsigned int n_failed = 0;

    utest_test_type = type;

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

    result->n_tests = n_tests;
    result->n_success = n_success;
    result->n_failed = n_failed;

    return all_success;
}

/*
 * Runs all registered test cases.
 */
bool unittest_run_all_tests(int argc, char** argv) {
    int prev_verbosity_level = -1;

    int i = 1;
    while (i < argc) {
        if ((strlen(argv[i]) == 3) && (argv[i][0] == 'v') && (argv[i][1] == '=')) {
            prev_verbosity_level = unittest_set_verbosity_level(argv[i][2] - '0');
        }
        i++;
    }

    // Rely on the TEST_ENV_NAME environment variable to tell us which
    // classes of tests we should execute.
    const char* test_type_str = getenv(TEST_ENV_NAME);
    test_type_t test_type;
    if (test_type_str == NULL) {
        // If we cannot access the environment variable, run all tests
        test_type = TEST_ALL;
    } else {
        test_type = atoi(test_type_str);
    }

    if (prev_verbosity_level >= 0)
        unittest_set_verbosity_level(prev_verbosity_level);

    struct test_result result;
    bool all_success = unittest_run_all_tests_etc(test_type, &result);

    unittest_printf_critical(
            "\n====================================================\n");
    unittest_printf_critical(
            "    CASES:  %d     SUCCESS:  %d     FAILED:  %d   ", result.n_tests, result.n_success, result.n_failed);
    unittest_printf_critical(
            "\n====================================================\n");

    return all_success;
}
