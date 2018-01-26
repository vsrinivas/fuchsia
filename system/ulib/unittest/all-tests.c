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
    return elem->test_case(false, NULL);
}

/*
 * Case name and test name are optional parameters that will cause only the
 * test[case]s matching the given name to run. If null, all test[case]s will
 * run.
 */
static bool unittest_run_all_tests_etc(
    const char* test_binary_name, test_type_t type,
    const char* case_name, const char* test_name, bool list_only) {
    unsigned int n_tests = 0;
    unsigned int n_failed = 0;

    utest_test_type = type;

    struct test_case_element* current = test_case_list;
    while (current) {
        if (!case_name || strcmp(current->name, case_name) == 0) {
            if (!current->test_case(list_only, test_name)) {
                current->failed_next = failed_test_case_list;
                failed_test_case_list = current;
                n_failed++;
            }
            n_tests++;
        }
        current = current->next;
    }

    // Don't print test results in list mode.
    if (list_only) return true;

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

static void print_help(void) {
    printf("Arguments: [--help] [--list] [[<test_case>] <test>]\n"
           "\n"
           "    --help\n"
           "        Prints this screen and exits.\n"
           "\n"
           "    --list\n"
           "        Prints the test names instead of running them.\n"
           "\n"
           "    --\n"
           "        Indicates end of switches. Anything following is interpreted as a\n"
           "        test case name.\n"
           "\n"
           "If <test_case> is specified, only the tests from the matching test\n"
           "case will be run. If additionally <test> is specified, only that\n"
           "specific test will be run. The test case and test names are case-\n"
           "sensitive exact matches (no regular expressions).\n");
}

/*
 * Runs all registered test cases.
 */
bool unittest_run_all_tests(int argc, char** argv) {
    bool list_tests_only = false;
    const char* case_matcher = NULL;
    const char* test_matcher = NULL;
    bool switches_allowed = true;

    int i = 1;
    while (i < argc) {
        if (switches_allowed && argv[i][0] == '-') {
            // Got a switch.
            if ((strlen(argv[i]) == 3) && (argv[i][0] == 'v') && (argv[i][1] == '=')) {
                unittest_set_verbosity_level(argv[i][2] - '0');
            } else if (strcmp(argv[i], "--help") == 0) {
                // Specifying --help in any way prints the help and exits.
                print_help();
                return 0;
            } else if (strcmp(argv[i], "--list") == 0) {
                list_tests_only = true;
            } else if (strcmp(argv[i], "--") == 0) {
                // "--" indicates no more switches.
                switches_allowed = false;
            } else {
                printf("Unknown switch \"%s\".\n\n", argv[i]);
                print_help();
                return 1;
            }
        } else {
            // Non-switch parameter.
            switches_allowed = false;
            if (!case_matcher) {
                case_matcher = argv[i];
            } else if (!test_matcher) {
                test_matcher = argv[i];
            } else {
                printf("Too many command line arguments.\n\n");
                print_help();
                return 1;
            }
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

    return unittest_run_all_tests_etc(argv[0], test_type, case_matcher,
                                      test_matcher, list_tests_only);
}
