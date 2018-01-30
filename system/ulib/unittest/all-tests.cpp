// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <unittest/unittest.h>

static test_case_element* test_case_list = nullptr;
static test_case_element* failed_test_case_list = nullptr;

// Registers a test case with the unit test framework.
void unittest_register_test_case(test_case_element* elem) {
    elem->next = test_case_list;
    test_case_list = elem;
}

bool unittest_run_one_test(test_case_element* elem, test_type_t type) {
    utest_test_type = type;
    return elem->test_case(false, nullptr);
}

// Case name and test name are optional parameters that will cause only the
// test[case]s matching the given name to run. If null, all test[case]s will
// run.
static bool unittest_run_all_tests_etc(const char* test_binary_name, test_type_t type,
                                       const char* case_name, const char* test_name,
                                       bool list_only) {
    unsigned int n_tests = 0;
    unsigned int n_failed = 0;

    utest_test_type = type;

    test_case_element* current = test_case_list;
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
    if (list_only)
        return true;

    unittest_printf_critical("====================================================\n");
    if (test_binary_name != nullptr && test_binary_name[0] != '\0') {
        unittest_printf_critical("Results for test binary \"%s\":\n", test_binary_name);
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
        test_case_element* failed = failed_test_case_list;
        while (failed) {
            unittest_printf_critical("        %s\n", failed->name);
            test_case_element* failed_next = failed->failed_next;
            failed->failed_next = nullptr;
            failed = failed_next;
        }
        failed_test_case_list = nullptr;
        unittest_printf_critical("\n");
    }
    unittest_printf_critical("    CASES:  %d     SUCCESS:  %d     FAILED:  %d   \n", n_tests,
                             n_tests - n_failed, n_failed);
    unittest_printf_critical("====================================================\n");
    return n_failed == 0;
}

static void print_help() {
    printf("Arguments: [--help] [--list] [--case <test_case>] [--test <test>]\n"
           "\n"
           "    --help\n"
           "        Prints this screen and exits.\n"
           "\n"
           "    --list\n"
           "        Prints the test names instead of running them.\n"
           "\n"
           "    --case <test_case>\n"
           "        Only the tests from the matching test case will be run.\n"
           "        <test_case> is case-sensitive; regex is not supported\n"
           "\n"
           "    --test <test>\n"
           "        Only the tests from the matching test will be run\n"
           "        <test> is case-sensitive; regex is not supported\n"
           "\n"
           "    v=<level>\n"
           "        Set the unit test verbosity level to <level>\n"
           "\n"
           );
}

/*
 * Runs all registered test cases.
 */
bool unittest_run_all_tests(int argc, char** argv) {
    bool list_tests_only = false;
    const char* case_matcher = nullptr;
    const char* test_matcher = nullptr;

    int i = 1;
    while (i < argc) {
        if (argv[i][0] == '-') {
            // Got a switch.
            if (strcmp(argv[i], "--help") == 0) {
                // Specifying --help in any way prints the help and exits.
                print_help();
                return 0;
            } else if (strcmp(argv[i], "--list") == 0) {
                list_tests_only = true;
            } else if (strcmp(argv[i], "--case") == 0) {
                if (i + 1 >= argc) {
                    print_help();
                    return 1;
                }
                case_matcher = argv[++i];
            } else if (strcmp(argv[i], "--test") == 0) {
                if (i + 1 >= argc) {
                    print_help();
                    return 0;
                }
                test_matcher = argv[++i];
            }
        } else if ((strlen(argv[i]) == 3) && (argv[i][0] == 'v') && (argv[i][1] == '=')) {
            unittest_set_verbosity_level(argv[i][2] - '0');
        } // Ignore other parameters
        i++;
    }

    // Rely on the TEST_ENV_NAME environment variable to tell us which
    // classes of tests we should execute.
    const char* test_type_str = getenv(TEST_ENV_NAME);
    test_type_t test_type;
    if (test_type_str == nullptr) {
        // If we cannot access the environment variable, run all tests
        test_type = TEST_ALL;
    } else {
        test_type = static_cast<test_type_t>(atoi(test_type_str));
    }

    return unittest_run_all_tests_etc(argv[0], test_type, case_matcher, test_matcher,
                                      list_tests_only);
}
