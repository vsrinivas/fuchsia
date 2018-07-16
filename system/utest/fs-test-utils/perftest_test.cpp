// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/mount.h>
#include <fs-test-utils/fixture.h>
#include <fs-test-utils/perftest.h>
#include <unittest/unittest.h>

namespace fs_test_utils {
namespace {

// File used to dump libs stdout. Allows verifying certain options.
constexpr char kFakeStdout[] = "/data/fake_stdout.txt";

bool ResultSetIsValid() {
    BEGIN_TEST;
    fbl::String err;
    PerformanceTestOptions p_options = PerformanceTestOptions::PerformanceTest();
    p_options.result_path = "some/path";
    ASSERT_TRUE(p_options.IsValid(&err), err.c_str());
    END_TEST;
}

bool SummaryPathSetIsValid() {
    BEGIN_TEST;
    fbl::String err;
    PerformanceTestOptions p_options = PerformanceTestOptions::PerformanceTest();
    p_options.summary_path = "some/path";
    ASSERT_TRUE(p_options.IsValid(&err), err.c_str());
    END_TEST;
}

bool PrintStatisticsSetIsValid() {
    BEGIN_TEST;
    fbl::String err;
    PerformanceTestOptions p_options = PerformanceTestOptions::PerformanceTest();
    p_options.print_statistics = true;
    ASSERT_TRUE(p_options.IsValid(&err), err.c_str());
    END_TEST;
}

bool NoOutputIsInvalid() {
    BEGIN_TEST;
    fbl::String err;
    PerformanceTestOptions p_options = PerformanceTestOptions::PerformanceTest();
    p_options.print_statistics = false;
    p_options.result_path.clear();
    p_options.summary_path.clear();

    ASSERT_FALSE(p_options.IsValid(&err), err.c_str());
    END_TEST;
}

bool InvalidOptionsReturnFalseAndPrintsUsage() {
    BEGIN_TEST;
    fbl::String err;
    char arg0[] = "/some/path/binary";
    char* argv[] = {arg0};
    PerformanceTestOptions p_options = PerformanceTestOptions::PerformanceTest();
    p_options.result_path = "some/path";
    FixtureOptions f_options = FixtureOptions::Default(DISK_FORMAT_MINFS);
    f_options.block_device_path = "some_path";
    f_options.use_ramdisk = true;

    ASSERT_FALSE(f_options.IsValid(&err));

    FILE* fp = fopen(kFakeStdout, "w");
    ASSERT_TRUE(fp);
    ASSERT_FALSE(ParseCommandLineArgs(1, argv, &f_options, &p_options, fp));
    fclose(fp);

    // Usage is printed on error.
    struct stat st;
    stat(kFakeStdout, &st);
    remove(kFakeStdout);
    ASSERT_GT(st.st_size, 0);
    END_TEST;
}

// Sanity check that we print into the stream when help option is provided.
bool HelpPrintsUsageMessage() {
    BEGIN_TEST;
    char arg0[] = "/some/path/binary";
    char arg1[] = "--help";
    char* argv[] = {arg0, arg1};
    fbl::String err;
    PerformanceTestOptions p_options = PerformanceTestOptions::PerformanceTest();
    FixtureOptions f_options = FixtureOptions::Default(DISK_FORMAT_MINFS);

    FILE* fp = fopen(kFakeStdout, "w");
    ASSERT_TRUE(fp);
    ASSERT_FALSE(ParseCommandLineArgs(2, argv, &f_options, &p_options, fp));
    fclose(fp);

    struct stat st;
    stat(kFakeStdout, &st);
    remove(kFakeStdout);
    ASSERT_GT(st.st_size, 0);
    END_TEST;
}

// Verifies that ParseCommandLineArgs actually sets the respective fields in the
// option structs.
bool OptionsAreOverwritten() {
    BEGIN_TEST;
    fbl::Vector<fbl::String> argvs = {
        "/some/binary",
        "-p",
        "--use_fvm",
        "--fvm_slice_size",
        "8192",
        "--use_ramdisk",
        "--ramdisk_block_size",
        "1024",
        "--ramdisk_block_count",
        "500",
        "--runs",
        "4",
        "--out",
        "some_path",
        "--summary_path",
        "other_path",
        "--print_statistics",
        "--fs",
        "blobfs",
    };
    const char* argv[argvs.size() + 1];
    for (size_t i = 0; i < argvs.size(); ++i) {
        argv[i] = argvs[i].data();
    }
    argv[argvs.size()] = nullptr;

    fbl::String err;
    PerformanceTestOptions p_options = PerformanceTestOptions::PerformanceTest();
    FixtureOptions f_options = FixtureOptions::Default(DISK_FORMAT_MINFS);

    FILE* fp = fopen(kFakeStdout, "w");
    ASSERT_TRUE(fp);
    ASSERT_TRUE(
        ParseCommandLineArgs(static_cast<int>(argvs.size()), argv, &f_options, &p_options, fp));
    fclose(fp);

    // Usage is not logged.
    struct stat st;
    stat(kFakeStdout, &st);
    remove(kFakeStdout);
    ASSERT_EQ(st.st_size, 0);

    // Parameters parsed.
    ASSERT_TRUE(f_options.block_device_path == "");
    ASSERT_TRUE(f_options.use_ramdisk);
    ASSERT_EQ(f_options.ramdisk_block_size, 1024);
    ASSERT_EQ(f_options.ramdisk_block_count, 500);
    ASSERT_TRUE(f_options.use_fvm);
    ASSERT_EQ(f_options.fvm_slice_size, 8192);
    ASSERT_EQ(f_options.fs_type, DISK_FORMAT_BLOBFS);

    ASSERT_FALSE(p_options.is_unittest);
    ASSERT_TRUE(p_options.result_path == "some_path");
    ASSERT_TRUE(p_options.summary_path == "other_path");
    ASSERT_TRUE(p_options.print_statistics);
    ASSERT_EQ(p_options.sample_count, 4);

    END_TEST;
}

bool RunTestCasesPreservesOrder() {
    BEGIN_TEST;
    PerformanceTestOptions p_options = PerformanceTestOptions::UnitTest();
    FixtureOptions f_options = FixtureOptions::Default(DISK_FORMAT_MINFS);
    p_options.sample_count = 1;
    fbl::Vector<int> calls;

    auto test_1 = [&calls](perftest::RepeatState* state, Fixture* fixture) {
        state->DeclareStep("test_1");
        while (state->KeepRunning()) {
            calls.push_back(1);
        }
        return true;
    };
    auto test_2 = [&calls](perftest::RepeatState* state, Fixture* fixture) {
        state->DeclareStep("test_2");
        while (state->KeepRunning()) {
            calls.push_back(2);
        }
        return true;
    };
    auto test_3 = [&calls](perftest::RepeatState* state, Fixture* fixture) {
        state->DeclareStep("test_3");
        while (state->KeepRunning()) {
            calls.push_back(3);
        }
        return true;
    };

    TestCaseInfo info;
    info.name = "MyTestCase";
    info.tests.push_back({fbl::move(test_1), "test_1", /*required_disk_space=*/0});
    info.tests.push_back({fbl::move(test_2), "test_2", 0});
    info.tests.push_back({fbl::move(test_3), "test_3", 0});
    info.teardown = false;

    fbl::Vector<TestCaseInfo> test_cases;
    test_cases.push_back(fbl::move(info));
    ASSERT_TRUE(RunTestCases(f_options, p_options, test_cases, /*out=*/nullptr));

    // Verify order is preserved.
    ASSERT_EQ(calls.size(), 3);
    ASSERT_EQ(calls[0], 1);
    ASSERT_EQ(calls[1], 2);
    ASSERT_EQ(calls[2], 3);

    END_TEST;
}

bool RunTestCasesPreservesOrderWithMultipleSamples() {
    BEGIN_TEST;
    PerformanceTestOptions p_options = PerformanceTestOptions::UnitTest();
    FixtureOptions f_options = FixtureOptions::Default(DISK_FORMAT_MINFS);
    p_options.is_unittest = false;
    p_options.sample_count = 10;
    fbl::Vector<int> calls;

    auto test_1 = [&calls](perftest::RepeatState* state, Fixture* fixture) {
        state->DeclareStep("test_1");
        while (state->KeepRunning()) {
            calls.push_back(1);
        }
        return true;
    };
    auto test_2 = [&calls](perftest::RepeatState* state, Fixture* fixture) {
        state->DeclareStep("test_2");
        while (state->KeepRunning()) {
            calls.push_back(2);
        }
        return true;
    };
    auto test_3 = [&calls](perftest::RepeatState* state, Fixture* fixture) {
        state->DeclareStep("test_3");
        while (state->KeepRunning()) {
            calls.push_back(3);
        }
        return true;
    };

    TestCaseInfo info;
    info.sample_count = 20;
    info.name = "MyTestCase";
    info.tests.push_back({fbl::move(test_1), "test_1", /*required_disk_space=*/0});
    info.tests.push_back({fbl::move(test_2), "test_2", 0});
    info.tests.push_back({fbl::move(test_3), "test_3", 0});
    info.teardown = false;

    fbl::Vector<TestCaseInfo> test_cases;
    test_cases.push_back(fbl::move(info));
    ASSERT_TRUE(RunTestCases(f_options, p_options, test_cases, /*out=*/nullptr));

    // Verify order is preserved.
    ASSERT_EQ(calls.size(), 60);
    for (int i = 0; i < 20; ++i) {
        ASSERT_EQ(calls[i], 1);
        ASSERT_EQ(calls[(20 + i)], 2);
        ASSERT_EQ(calls[(40 + i)], 3);
    }

    END_TEST;
}

bool RunTestCasesWritesResultsAndStatistics() {
    BEGIN_TEST;
    PerformanceTestOptions p_options = PerformanceTestOptions::PerformanceTest();
    p_options.result_path = "/data/results.json";
    p_options.summary_path = "/data/summary.txt";
    p_options.print_statistics = true;

    FixtureOptions f_options = FixtureOptions::Default(DISK_FORMAT_MINFS);
    p_options.sample_count = 1;

    auto test_1 = [](perftest::RepeatState* state, Fixture* fixture) {
        state->DeclareStep("test_1");
        state->DeclareStep("test_2");
        while (state->KeepRunning()) {
            state->NextStep();
        }
        return true;
    };

    TestCaseInfo info;
    info.name = "MyTestCase";
    info.tests.push_back({fbl::move(test_1), "test_1", /*required_disk_space=*/0});
    info.teardown = false;

    fbl::Vector<TestCaseInfo> test_cases;
    test_cases.push_back(fbl::move(info));

    FILE* fp = fopen(kFakeStdout, "w+");
    ASSERT_TRUE(fp);
    ASSERT_TRUE(RunTestCases(f_options, p_options, test_cases, fp));
    fseek(fp, 0, SEEK_SET);
    // Look for test_1.test_1 in fake_std.txt (test_name.step_name).
    char* buffer = nullptr;
    size_t length = 0;
    ssize_t read;
    bool found_1 = false;
    bool found_2 = false;
    while ((read = getline(&buffer, &length, fp)) != -1 && (!found_1 || !found_2)) {
        if (strstr(buffer, "test_1.test_1")) {
            found_1 = true;
        } else if (strstr(buffer, "test_1.test_2")) {
            found_2 = true;
        }
        free(buffer);
        buffer = nullptr;
        length = 0;
    }
    free(buffer);
    buffer = nullptr;
    remove(kFakeStdout);
    fclose(fp);
    EXPECT_TRUE(found_1);
    EXPECT_TRUE(found_2);

    struct stat st;
    stat(p_options.result_path.c_str(), &st);
    remove(p_options.result_path.c_str());
    EXPECT_GT(st.st_size, 0);

    stat(p_options.summary_path.c_str(), &st);
    remove(p_options.summary_path.c_str());
    EXPECT_GT(st.st_size, 0);

    END_TEST;
}

BEGIN_TEST_CASE(FsPerformanceTestOptions)
RUN_TEST(ResultSetIsValid)
RUN_TEST(SummaryPathSetIsValid)
RUN_TEST(PrintStatisticsSetIsValid)
RUN_TEST(NoOutputIsInvalid)
END_TEST_CASE(FsPerformanceTestOptions)

BEGIN_TEST_CASE(FsPerformanceTestLib)
RUN_TEST(InvalidOptionsReturnFalseAndPrintsUsage)
RUN_TEST(OptionsAreOverwritten)
RUN_TEST(HelpPrintsUsageMessage)
RUN_TEST(RunTestCasesPreservesOrder)
RUN_TEST(RunTestCasesPreservesOrderWithMultipleSamples)
RUN_TEST(RunTestCasesWritesResultsAndStatistics)
END_TEST_CASE(FsPerformanceTestLib)

} // namespace
} // namespace fs_test_utils
