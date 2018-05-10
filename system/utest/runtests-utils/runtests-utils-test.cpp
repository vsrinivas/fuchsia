// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>
#include <runtests-utils/runtests-utils.h>
#include <unittest/unittest.h>

namespace runtests {
namespace {

static char* my_own_path;
static constexpr size_t kOneMegabyte = 1 << 20;
static constexpr char kPrintToStdOutAndSucceedFlag[] = "--print-to-std-out-and-succeed";
static constexpr char kPrintToStdErrAndFailFlag[] = "--print-to-std-err-and-fail";
static constexpr char kExpectedOutput[] = "Expect this!\n";
// Arbitrary, but different from EXIT_FAILURE so we can be sure
// which code path gets taken in main().
static constexpr int kFailureReturnCode = 77;

bool RunTestSuccess() {
    BEGIN_TEST;
    const char* argv[] = {my_own_path, kPrintToStdOutAndSucceedFlag};
    const Result result = RunTest(argv, 2, nullptr);
    EXPECT_STR_EQ(my_own_path, result.name.c_str());
    EXPECT_EQ(SUCCESS, result.launch_status);
    EXPECT_EQ(0, result.return_code);
    END_TEST;
}

bool RunTestSuccessWithStdout() {
    BEGIN_TEST;
    // A reasonable guess that the process won't output more than this.
    char* buf = static_cast<char *>(malloc(kOneMegabyte));
    FILE* buf_file = fmemopen(buf, kOneMegabyte, "w");
    const char* argv[] = {my_own_path, kPrintToStdOutAndSucceedFlag};
    const Result result = RunTest(argv, 2, buf_file);
    fclose(buf_file);
    ASSERT_STR_EQ(kExpectedOutput, buf);
    EXPECT_STR_EQ(my_own_path, result.name.c_str());
    EXPECT_EQ(SUCCESS, result.launch_status);
    EXPECT_EQ(0, result.return_code);
    END_TEST;
}

bool RunTestFailureWithStderr() {
    BEGIN_TEST;
    char buf[1024];
    FILE* buf_file = fmemopen(buf, sizeof(buf), "w");
    // In this case, extra args will cause the binary to exit with failure and print something
    // to stderr.
    const char* argv[] = {my_own_path, kPrintToStdErrAndFailFlag};
    const Result result = RunTest(argv, 2, buf_file);
    fclose(buf_file);
    ASSERT_STR_EQ(kExpectedOutput, buf);
    EXPECT_STR_EQ(my_own_path, result.name.c_str());
    EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, result.launch_status);
    EXPECT_EQ(kFailureReturnCode, result.return_code);
    END_TEST;
}

bool RunTestFailureToLoadFile() {
    BEGIN_TEST;
    const char* argv[] = {"i/do/not/exist/"};
    const Result result = RunTest(argv, 1, nullptr);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(FAILED_TO_LAUNCH, result.launch_status);
    END_TEST;
}

BEGIN_TEST_CASE(RunTest)
RUN_TEST(RunTestSuccess)
RUN_TEST(RunTestSuccessWithStdout)
RUN_TEST(RunTestFailureWithStderr)
RUN_TEST(RunTestFailureToLoadFile)
END_TEST_CASE(RunTest)

}  // namespace
}  // namespace resulttests

int main(int argc, char** argv) {
    // Record the path to the path to this binary in a global variable so it can be
    // re-used in the tests.
    // We couldn't figure out how to get the makefiles to produce a
    // helper binary in a subdirectory, so we're using this binary itself
    // as the helper binary to test various cases.
    runtests::my_own_path = argv[0];
    if (argc == 2 && !strcmp(runtests::kPrintToStdOutAndSucceedFlag, argv[1])) {
        fprintf(stdout, runtests::kExpectedOutput);
        return EXIT_SUCCESS;
    } else if (argc == 2 && !strcmp(runtests::kPrintToStdErrAndFailFlag, argv[1])) {
        fprintf(stderr, runtests::kExpectedOutput);
        return runtests::kFailureReturnCode;
    }
    return unittest_run_all_tests(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
}
