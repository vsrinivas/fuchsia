// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <unittest/unittest.h>

#include "watchdog.h"

static test_case_element* test_case_list = nullptr;
static test_case_element* failed_test_case_list = nullptr;

static unittest_help_printer_type* print_test_help = nullptr;

// Registers a test case with the unit test framework.
void unittest_register_test_case(test_case_element* elem) {
    elem->next = test_case_list;
    test_case_list = elem;
}

bool unittest_run_one_test(test_case_element* elem, test_type_t type) {
    utest_test_type = type;
    return elem->test_case(false, nullptr);
}

void unittest_register_test_help_printer(unittest_help_printer_type* func) {
    print_test_help = func;
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

static void print_help(const char* prog_name, FILE* f) {
    fprintf(f, "Usage: %s [OPTIONS]\n", prog_name);
    fprintf(f, "\nOptions:\n"
            "  -h | --help\n"
            "      Prints this text and exits.\n"
            "\n"
            "  --list\n"
            "      Prints the test names instead of running them.\n"
            "\n"
            "  --case <test_case>\n"
            "      Only the tests from the matching test case will be run.\n"
            "      <test_case> is case-sensitive; regex is not supported\n"
            "\n"
            "  --test <test>\n"
            "      Only the tests from the matching test will be run\n"
            "      <test> is case-sensitive; regex is not supported\n"
            "\n"
            "  v=<level>\n"
            "      Set the unit test verbosity level to <level>\n"
            );
    if (print_test_help) {
        fprintf(f, "\nTest-specific options:\n");
        print_test_help(f);
    }
    fprintf(f, "\n"
            "Environment variables:\n"
            "  %s=<types-mask>\n"
            "      Specifies the types of tests to run.\n"
            "      Must be the OR of the following values, in base 10:\n"
            "        0x01 = small\n"
            "        0x02 = medium\n"
            "        0x04 = large\n"
            "        0x08 = performance\n"
            "      If unspecified then all tests are run.\n"
            "\n"
            "  %s=<base-timeout-in-seconds>\n"
            "      Specifies the base timeout which is the timeout of\n"
            "      small tests. Other test types have a timeout that is a\n"
            "      multiple of this amount. If unspecified the default base\n"
            "      timeout is %d seconds.\n",
            TEST_ENV_NAME, WATCHDOG_ENV_NAME,
            DEFAULT_BASE_TIMEOUT_SECONDS);
    fprintf(f,
            "      A scaling factor is applied to the base timeout:\n"
            "        Small       - x %d\n"
            "        Medium      - x %d\n"
            "        Large       - x %d\n"
            "        Performance - x %d\n",
            TEST_TIMEOUT_FACTOR_SMALL, TEST_TIMEOUT_FACTOR_MEDIUM,
            TEST_TIMEOUT_FACTOR_LARGE, TEST_TIMEOUT_FACTOR_PERFORMANCE);
}

/*
 * Runs all registered test cases.
 */
bool unittest_run_all_tests(int argc, char** argv) {
    const char* prog_name = basename(argv[0]);
    bool list_tests_only = false;
    const char* case_matcher = nullptr;
    const char* test_matcher = nullptr;

    int i = 1;
    while (i < argc) {
        const char* arg = argv[i];
        if (arg[0] == '-') {
            // Got a switch.
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                // Specifying --help at any point prints the help and exits.
                print_help(prog_name, stdout);
                return true;
            } else if (strcmp(arg, "--list") == 0) {
                list_tests_only = true;
            } else if (strcmp(arg, "--case") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: missing arg to %s\n", arg);
                    return false;
                }
                case_matcher = argv[++i];
            } else if (strcmp(arg, "--test") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: missing arg to %s\n", arg);
                    return false;
                }
                test_matcher = argv[++i];
            }
        } else if ((strlen(arg) == 3) && (arg[0] == 'v') && (arg[1] == '=')) {
            unittest_set_verbosity_level(arg[2] - '0');
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

    // Rely on the WATCHDOG_ENV_NAME environment variable to tell us
    // the timeout to use.
    const char* watchdog_timeout_str = getenv(WATCHDOG_ENV_NAME);
    if (watchdog_timeout_str != nullptr) {
        char* end;
        long timeout = strtol(watchdog_timeout_str, &end, 0);
        if (*watchdog_timeout_str == '\0' || *end != '\0' ||
            timeout < 0 || timeout > INT_MAX) {
            fprintf(stderr, "Error: bad watchdog timeout\n");
            return false;
        }
        watchdog_set_base_timeout(static_cast<int>(timeout));
    }

    watchdog_initialize();

    auto result = unittest_run_all_tests_etc(argv[0], test_type, case_matcher, test_matcher,
                                             list_tests_only);

    watchdog_terminate();
    return result;
}
