// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <fs-test-utils/fixture.h>
#include <perftest/perftest.h>
#include <unittest/unittest.h>
#include <zircon/errors.h>

namespace fs_test_utils {

// Options that define how a performance test is run.
struct PerformanceTestOptions {
    // Returns options which allow fast execution. No measurements will
    // be recorded.
    static PerformanceTestOptions UnitTest() {
        PerformanceTestOptions options;
        options.result_path = "";
        options.summary_path = "";
        options.sample_count = 1;
        options.print_statistics = false;
        options.is_unittest = true;
        return options;
    }

    static PerformanceTestOptions PerformanceTest() {
        PerformanceTestOptions options = UnitTest();
        options.is_unittest = false;
        return options;
    }

    // Returns true if the current set of options is valid.
    bool IsValid(fbl::String* err) const;

    // Path to output file.
    fbl::String result_path;

    // Path to summary statistics file.
    fbl::String summary_path;

    // Number of times to sample each operation. An operation is either a test or a stage that
    // will be executed multiple times.
    uint32_t sample_count;

    // Whether summary statistics should be printed or not.
    bool print_statistics;

    // True if we are running in unittest mode.
    // Benchmark tests are required to be implemented in such a way, that unittest mode validates
    // that the workflow is correct, and exit quickly.
    bool is_unittest;
};

struct TestInfo {
    // Funcion that executes the test.
    fbl::Function<bool(perftest::RepeatState*, Fixture*)> test_fn;

    // Name of the test.
    fbl::String name;

    // Estimation of the required disk space for this test. If set to 0, will
    // always be executed (may lead to OOS or OOM(ramdisk) errors.
    // This is optional.
    size_t required_disk_space = 0;

    // Number of times to run this test. Will overwrite |PerformanceTestOptions::sample_count|
    // if set and wil be ignored if in unittest mode.
    // This is optional.
    uint32_t sample_count = 0;
};

struct TestCaseInfo {
    // Ordered list of tests be executed as part of this test case.
    fbl::Vector<TestInfo> tests;

    // TestCase name.
    fbl::String name;

    // Whether there should be teardown between each test. If your tests depend
    // on the leftover state in the underlying FS from previous state, set this to false.
    bool teardown;
};

// Returns true if the parsed args should trigger a test run. The usage information is
// written to |out|.
// Note: |performance_test| will be completely overwritten by the data parsed. If caller
// wants to fix a  |performance_test|, it should be done after parsing.
bool ParseCommandLineArgs(int argc, const char* const* argv, FixtureOptions* fixture_options,
                          PerformanceTestOptions* performance_test, FILE* out = stdout);

// Runs all tests in the given testcase.
// Test's status and results will be printed into |out|. Errors will still be logged into
// |stdout|.
bool RunTestCases(const FixtureOptions& fixture_options,
                  const PerformanceTestOptions& performance_test_options,
                  const fbl::Vector<TestCaseInfo>& test_case, FILE* out = stdout);

} // namespace fs_test_utils
