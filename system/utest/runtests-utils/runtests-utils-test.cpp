// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex.h>
#include <stdio.h>
#include <unistd.h>
#include <runtests-utils/runtests-utils.h>
#include <unittest/unittest.h>

namespace runtests {
namespace {

// We're using the ps binary as our test binary for now because:
// * It's present by default, thus likely to be there when we run the test.
// * It succeeds when run with no arguments.
// * It prints predictable output when run with no arguments.
// * It can be made to fail by specifying additional arguments.
// Ideally we'd use a custom test binary that we can control the behavior of and not rely
// on any external binaries we haven't ensured are there, but this works for now.
static constexpr char kTestBinaryPath[] = "/boot/bin/ps";
// ps's output is something like "TASK             PSS PRIVATE  SHARED NAME"
static constexpr char kTestBinarySuccessStdOut[] = "TASK\\s+";
static constexpr signed char kNoVerbosity = -1;
static constexpr size_t kOneMegabyte = 1 << 20;

bool run_test_success() {
    BEGIN_TEST;
    list_node_t tests = LIST_INITIAL_VALUE(tests);
    ASSERT_TRUE(run_test(kTestBinaryPath, kNoVerbosity, nullptr, &tests));
    test_t* test = nullptr;
    test_t* temp = nullptr;
    size_t list_len = 0;
    list_for_every_entry_safe(&tests, test, temp, test_t, node) {
        EXPECT_STR_EQ(kTestBinaryPath, test->name);
        EXPECT_EQ(SUCCESS, test->result);
        EXPECT_EQ(0, test->rc);
        list_len += 1;
    }
    EXPECT_EQ(1, list_len);
    END_TEST;
}

bool run_test_success_with_stdout() {
    BEGIN_TEST;
    list_node_t tests = LIST_INITIAL_VALUE(tests);
    // A reasonable guess that the process won't output more than this.
    char* buf = static_cast<char *>(malloc(kOneMegabyte));
    FILE* buf_file = fmemopen(buf, kOneMegabyte, "w");
    ASSERT_TRUE(run_test(kTestBinaryPath, kNoVerbosity, buf_file, &tests));
    fclose(buf_file);
    regex_t re;
    ASSERT_EQ(0, regcomp(&re, kTestBinarySuccessStdOut, REG_EXTENDED | REG_NOSUB));
    const int regexec_status = regexec(&re, buf, (size_t) 0, nullptr, 0);
    ASSERT_EQ(0, regexec_status);
    regfree(&re);
    test_t* test = nullptr;
    test_t* temp = nullptr;
    size_t list_len = 0;
    list_for_every_entry_safe(&tests, test, temp, test_t, node) {
        EXPECT_STR_EQ(kTestBinaryPath, test->name);
        EXPECT_EQ(SUCCESS, test->result);
        EXPECT_EQ(0, test->rc);
        list_len += 1;
    }
    EXPECT_EQ(1, list_len);
    END_TEST;
}

bool run_test_failure_with_stderr() {
    BEGIN_TEST;
    list_node_t tests = LIST_INITIAL_VALUE(tests);
    char buf[1024];
    FILE* buf_file = fmemopen(buf, sizeof(buf), "w");
    // Specifying verbosity such that extra args get passed to the test binary.
    // In this case, these args will cause the binary to exit with failure and print something
    // to stderr.
    ASSERT_FALSE(run_test(kTestBinaryPath, kNoVerbosity + 1, buf_file, &tests));
    fclose(buf_file);
    // Don't want to hard code the binary's usage string.
    // TODO(garymm): When I control the binary that gets run, assert the stderr is correct.
    ASSERT_GT(strlen(buf), 0);
    test_t* test = nullptr;
    test_t* temp = nullptr;
    size_t list_len = 0;
    list_for_every_entry_safe(&tests, test, temp, test_t, node) {
        EXPECT_STR_EQ(kTestBinaryPath, test->name);
        EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, test->result);
        EXPECT_NE(0, test->rc);
        list_len += 1;
    }
    EXPECT_EQ(1, list_len);
    END_TEST;
}

bool run_test_failure_to_load_file() {
    BEGIN_TEST;
    list_node_t tests = LIST_INITIAL_VALUE(tests);
    EXPECT_FALSE(run_test("i/do/not/exist/", kNoVerbosity, nullptr, &tests));
    EXPECT_EQ(0, list_length(&tests));
    END_TEST;
}

bool record_test_result_succeeds() {
    BEGIN_TEST;
    list_node_t tests = LIST_INITIAL_VALUE(tests);
    record_test_result(&tests, "0", SUCCESS, 0);
    record_test_result(&tests, "1", FAILED_NONZERO_RETURN_CODE, 1);
    int i = 0;
    test_t* test = nullptr;
    test_t* temp = nullptr;
    list_for_every_entry_safe(&tests, test, temp, test_t, node) {
        if (i == 0) {
            EXPECT_STR_EQ("0", test->name);
            EXPECT_EQ(SUCCESS, test->result);
        } else {
            EXPECT_STR_EQ("1", test->name);
            EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, test->result);
        }
        EXPECT_EQ(i, test->rc);
        i += 1;
    }
    EXPECT_EQ(2, i);
    END_TEST;
}

BEGIN_TEST_CASE(test_results)
RUN_TEST(record_test_result_succeeds)
END_TEST_CASE(test_results)

BEGIN_TEST_CASE(run_test)
RUN_TEST(run_test_success)
RUN_TEST(run_test_success_with_stdout)
RUN_TEST(run_test_failure_with_stderr)
RUN_TEST(run_test_failure_to_load_file)
END_TEST_CASE(run_test)

}  // namespace
}  // namespace runtests

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
}
