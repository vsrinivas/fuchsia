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
// * It's present by default, thus likely to be there when we result the test.
// * It succeeds when result with no arguments.
// * It prints predictable output when result with no arguments.
// * It can be made to fail by specifying additional arguments.
// Ideally we'd use a custom test binary that we can control the behavior of and not rely
// on any external binaries we haven't ensured are there, but this works for now.
static constexpr char kTestBinaryPath[] = "/boot/bin/ps";
// ps's output is something like "TASK             PSS PRIVATE  SHARED NAME"
static constexpr char kTestBinarySuccessStdOut[] = "TASK\\s+";
static constexpr signed char kNoVerbosity = -1;
static constexpr size_t kOneMegabyte = 1 << 20;

bool RunTestSuccess() {
    BEGIN_TEST;
    const Result result = RunTest(kTestBinaryPath, kNoVerbosity, nullptr);
    EXPECT_STR_EQ(kTestBinaryPath, result.name.c_str());
    EXPECT_EQ(SUCCESS, result.launch_status);
    EXPECT_EQ(0, result.return_code);
    END_TEST;
}

bool RunTestSuccessWithStdout() {
    BEGIN_TEST;
    // A reasonable guess that the process won't output more than this.
    char* buf = static_cast<char *>(malloc(kOneMegabyte));
    FILE* buf_file = fmemopen(buf, kOneMegabyte, "w");
    const Result result = RunTest(kTestBinaryPath, kNoVerbosity, buf_file);
    fclose(buf_file);
    regex_t re;
    ASSERT_EQ(0, regcomp(&re, kTestBinarySuccessStdOut, REG_EXTENDED | REG_NOSUB));
    const int regexec_status = regexec(&re, buf, (size_t) 0, nullptr, 0);
    ASSERT_EQ(0, regexec_status);
    regfree(&re);
    EXPECT_STR_EQ(kTestBinaryPath, result.name.c_str());
    EXPECT_EQ(SUCCESS, result.launch_status);
    EXPECT_EQ(0, result.return_code);
    END_TEST;
}

bool RunTestFailureWithStderr() {
    BEGIN_TEST;
    char buf[1024];
    FILE* buf_file = fmemopen(buf, sizeof(buf), "w");
    // Specifying verbosity such that extra args get passed to the test binary.
    // In this case, these args will cause the binary to exit with failure and print something
    // to stderr.
    const Result result = RunTest(kTestBinaryPath, kNoVerbosity + 1, buf_file);
    fclose(buf_file);
    // Don't want to hard code the binary's usage string.
    // TODO(garymm): When I control the binary that gets result, assert the stderr is correct.
    ASSERT_GT(strlen(buf), 0);
    EXPECT_STR_EQ(kTestBinaryPath, result.name.c_str());
    EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, result.launch_status);
    EXPECT_NE(0, result.return_code);
    END_TEST;
}

bool RunTestFailureToLoadFile() {
    BEGIN_TEST;
    const char non_existent_test[] = "i/do/not/exist/";
    const Result result = RunTest(non_existent_test, kNoVerbosity, nullptr);
    EXPECT_STR_EQ(non_existent_test, result.name.c_str());
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
    return unittest_run_all_tests(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
}
