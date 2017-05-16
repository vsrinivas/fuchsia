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

bool unittest_run_one_test(struct test_case_element* elem, test_type_t type) {
    utest_test_type = type;
    return elem->test_case();
}

static bool unittest_run_all_tests_etc(
    const char* test_binary_name, test_type_t type) {
    unsigned int n_tests = 0;
    unsigned int n_failed = 0;

    utest_test_type = type;

    struct test_case_element* current = test_case_list;
    while (current) {
        if (!current->test_case()) {
            current->failed_next = failed_test_case_list;
            failed_test_case_list = current;
            n_failed++;
        }
        current = current->next;
        n_tests++;
    }

    unittest_printf_critical(
        "====================================================\n");
    if (test_binary_name != NULL && test_binary_name[0] != '\0') {
        unittest_printf_critical(
            "Results for test binary \"%s\":\n", test_binary_name);
    } else {
        // argv[0] can be null for binaries that run as userboot,
        // like core-tests.
        unittest_printf_critical("Results:\n");
    }
    if (n_failed == 0) {
        unittest_printf_critical("    SUCCESS!  All test cases passed!\n");
    } else {
        unittest_printf_critical("\n");
        unittest_printf_critical("    The following test cases failed:\n");
        struct test_case_element* failed = failed_test_case_list;
        while (failed) {
            unittest_printf_critical("        %s\n", failed->name);
            struct test_case_element* failed_next =
                failed->failed_next;
            failed->failed_next = NULL;
            failed = failed_next;
        }
        failed_test_case_list = NULL;
        unittest_printf_critical("\n");
    }
    unittest_printf_critical(
        "    CASES:  %d     SUCCESS:  %d     FAILED:  %d   \n",
        n_tests, n_tests - n_failed, n_failed);
    unittest_printf_critical(
        "====================================================\n");
    return n_failed == 0;
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

    return unittest_run_all_tests_etc(argv[0], test_type);
}
