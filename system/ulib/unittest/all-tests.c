// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
bool unittest_run_all_tests(void) {
    unsigned int n_tests = 0;
    unsigned int n_success = 0;
    unsigned int n_failed = 0;

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
        unittest_printf("SUCCESS!  All test cases passed!\n");
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

    unittest_printf("\n====================================================\n");
    unittest_printf("    CASES:  %d     SUCCESS:  %d     FAILED:  %d   ",
                    n_tests, n_success, n_failed);
    unittest_printf("\n====================================================\n");

    return all_success;
}
