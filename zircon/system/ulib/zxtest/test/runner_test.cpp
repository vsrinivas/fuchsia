// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

#include <cerrno>

#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

#include <zircon/assert.h>
#include <zxtest/base/environment.h>
#include <zxtest/base/reporter.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test.h>

namespace zxtest {

using internal::TestDriver;
using internal::TestDriverImpl;

namespace test {
namespace {

constexpr char kTestName[] = "TestName";
constexpr char kTestName2[] = "TestName2";
constexpr char kTestCaseName[] = "TestCase";
constexpr char kTestCaseName2[] = "TestCase2";
constexpr char kFileName[] = "filename.cpp";
constexpr int kLineNumber = 20;

// Test fixture that runs a given closure.
class FakeTest : public zxtest::Test {
public:
    static fbl::Function<std::unique_ptr<Test>(TestDriver*)> MakeFactory(int* counter) {
        return [counter](TestDriver* driver) {
            std::unique_ptr<FakeTest> test = zxtest::Test::Create<FakeTest>(driver);
            test->counter_ = counter;
            return test;
        };
    }

private:
    void TestBody() final { ++*counter_; }

    int* counter_;
};

class FailingTest : public zxtest::Test {
public:
    static fbl::Function<std::unique_ptr<Test>(TestDriver*)> MakeFactory(Runner* runner) {
        return [runner](TestDriver* driver) {
            std::unique_ptr<FailingTest> test = zxtest::Test::Create<FailingTest>(driver);
            test->runner_ = runner;
            return test;
        };
    }

private:
    void TestBody() {
        Assertion assertion("eq", "a", "1", "b", "2",
                            {.filename = __FILE__, .line_number = __LINE__},
                            /*is_fatal=*/true);
        runner_->NotifyAssertion(assertion);
    }
    Runner* runner_;
};

} // namespace

void RunnerRegisterTest() {
    Runner runner(Reporter(/*stream*/ nullptr));

    TestRef ref =
        runner.RegisterTest<Test, FakeTest>(kTestCaseName, kTestName, kFileName, kLineNumber);

    ZX_ASSERT_MSG(ref.test_case_index == 0, "TestRef::test_case_index is wrong.\n");
    ZX_ASSERT_MSG(ref.test_index == 0, "TestRef::test_index is wrong.\n");

    const TestInfo& info = runner.GetTestInfo(ref);

    ZX_ASSERT_MSG(info.name() == kTestName, "Test Registered with wrong name.\n");
    ZX_ASSERT_MSG(info.location().filename == kFileName,
                  "Test registered at wrong file location.\n");
    ZX_ASSERT_MSG(info.location().line_number == kLineNumber,
                  "Test registered at wrong line number in correct file location.\n");
    ZX_ASSERT_MSG(runner.summary().registered_test_count == 1,
                  "Test failed to register correctly.\n");
    ZX_ASSERT_MSG(runner.summary().registered_test_case_count == 1,
                  "TestCase failed to register correctly.\n");
}

void RunnerRegisterTestWithCustomFactory() {
    Runner runner(Reporter(/*stream*/ nullptr));
    int test_counter = 0;

    TestRef ref = runner.RegisterTest<Test, FakeTest>(
        kTestCaseName, kTestName, kFileName, kLineNumber, FakeTest::MakeFactory(&test_counter));

    ZX_ASSERT_MSG(ref.test_case_index == 0, "TestRef::test_case_index is wrong.\n");
    ZX_ASSERT_MSG(ref.test_index == 0, "TestRef::test_index is wrong.\n");

    const TestInfo& info = runner.GetTestInfo(ref);

    ZX_ASSERT_MSG(info.name() == kTestName, "Test Registered with wrong name.\n");
    ZX_ASSERT_MSG(info.location().filename == kFileName,
                  "Test registered at wrong file location.\n");
    ZX_ASSERT_MSG(info.location().line_number == kLineNumber,
                  "Test registered at wrong line number in correct file location.\n");
    ZX_ASSERT_MSG(runner.summary().registered_test_count == 1,
                  "Test failed to register correctly.\n");
    ZX_ASSERT_MSG(runner.summary().registered_test_case_count == 1,
                  "TestCase failed to register correctly.\n");
}

void RunnerRunAllTests() {
    Runner runner(Reporter(/*stream*/ nullptr));
    int test_counter = 0;
    int test_2_counter = 0;

    TestRef ref = runner.RegisterTest<Test, FakeTest>(
        kTestCaseName, kTestName, kFileName, kLineNumber, FakeTest::MakeFactory(&test_counter));
    TestRef ref2 = runner.RegisterTest<Test, FakeTest>(
        "TestCase2", kTestName, kFileName, kLineNumber, FakeTest::MakeFactory(&test_2_counter));

    ZX_ASSERT_MSG(ref.test_case_index != ref2.test_case_index,
                  "Different TestCase share same index.\n");

    // Verify that the runner actually claims to hold two tests from one test case.
    ZX_ASSERT_MSG(runner.summary().registered_test_count == 2,
                  "Test failed to register correctly.\n");
    ZX_ASSERT_MSG(runner.summary().registered_test_case_count == 2,
                  "TestCase failed to register correctly.\n");

    ZX_ASSERT_MSG(runner.Run(Runner::kDefaultOptions) == 0, "Test Execution Failed.\n");

    // Check that the active count reflects a filter matching all.
    ZX_ASSERT_MSG(runner.summary().active_test_count == 2, "Failed to register both tests.\n");
    ZX_ASSERT_MSG(runner.summary().active_test_case_count == 2, "Failed to register both tests.\n");

    // Check that both tests were executed once.
    ZX_ASSERT_MSG(test_counter == 1, "test was not executed.\n");
    ZX_ASSERT_MSG(test_2_counter == 1, "test_2 was not executed.\n");
}

// This test Will increase |counter_| each time is executed, until |*counter_| equals |fail_at|.
// When this happens, an assertion will be dispatched to |runner_|. This allows testing for
// infinite iterations and breaking on failure.
template <int fail_at>
class FakeRepeatingTest : public zxtest::Test {
public:
    static fbl::Function<std::unique_ptr<Test>(TestDriver*)> MakeFactory(Runner* runner,
                                                                         int* counter) {
        return [counter, runner](TestDriver* driver) {
            std::unique_ptr<FakeRepeatingTest> test =
                zxtest::Test::Create<FakeRepeatingTest>(driver);
            test->counter_ = counter;
            test->runner_ = runner;
            return test;
        };
    }

private:
    void TestBody() final {
        ++*counter_;
        if (*counter_ >= fail_at) {
            Assertion assertion("eq", "a", "1", "b", "2",
                                {.filename = __FILE__, .line_number = __LINE__},
                                /*is_fatal=*/true);
            runner_->NotifyAssertion(assertion);
        }
    }

    int* counter_;
    Runner* runner_;
};

void RunnerRunAllTestsUntilFailure() {
    Runner runner(Reporter(/*stream*/ nullptr));
    int test_counter = 0;
    constexpr int kAttemptsUntilFailure = 10;

    runner.RegisterTest<Test, FakeRepeatingTest<kAttemptsUntilFailure>>(
        kTestCaseName, kTestName, kFileName, kLineNumber,
        FakeRepeatingTest<kAttemptsUntilFailure>::MakeFactory(&runner, &test_counter));

    // Verify that the runner actually claims to hold two tests from one test case.
    ZX_ASSERT_MSG(runner.summary().registered_test_count == 1,
                  "Test failed to register correctly.\n");
    ZX_ASSERT_MSG(runner.summary().registered_test_case_count == 1,
                  "TestCase failed to register correctly.\n");

    Runner::Options options = Runner::kDefaultOptions;
    options.break_on_failure = true;
    options.repeat = -1;
    ZX_ASSERT_MSG(runner.Run(options) != 0, "Test Execution Should Fail.\n");

    // Check that the active count reflects a filter matching all.
    ZX_ASSERT_MSG(runner.summary().active_test_count == 1, "Failed to register test.\n");
    ZX_ASSERT_MSG(runner.summary().active_test_case_count == 1, "Failed to register test.\n");

    // Check that both tests were executed 10 times before they failed.
    ZX_ASSERT_MSG(test_counter == kAttemptsUntilFailure, "test was not executed enough.\n");
}

class FakeEnv : public zxtest::Environment {
public:
    FakeEnv(int* curr_setup, int* curr_tear_down) {
        this->curr_setup_ = curr_setup;
        this->curr_tear_down_ = curr_tear_down;
    }
    ~FakeEnv() final {}

    void SetUp() final {
        set_up_order_ = *curr_setup_;
        ++(*curr_setup_);
    }

    void TearDown() final {
        tear_down_order_ = *curr_tear_down_;
        ++(*curr_tear_down_);
    }

    int set_up_order() const { return set_up_order_; }
    int tear_down_order() const { return tear_down_order_; }

private:
    int set_up_order_ = 0;
    int tear_down_order_ = 0;
    int* curr_setup_ = nullptr;
    int* curr_tear_down_ = nullptr;
};

void RunnerSetUpAndTearDownEnvironmentsTests() {
    Runner runner(Reporter(/*stream*/ nullptr));
    int test_counter = 0;
    int tear_down_counter = 1;
    int set_up_counter = 1;
    std::unique_ptr<FakeEnv> first = std::make_unique<FakeEnv>(&set_up_counter, &tear_down_counter);
    std::unique_ptr<FakeEnv> second =
        std::make_unique<FakeEnv>(&set_up_counter, &tear_down_counter);
    FakeEnv* first_ptr = first.get();
    FakeEnv* second_ptr = second.get();

    [[maybe_unused]] TestRef ref = runner.RegisterTest<Test, FakeTest>(
        kTestCaseName, kTestName, kFileName, kLineNumber, FakeTest::MakeFactory(&test_counter));

    runner.AddGlobalTestEnvironment(std::move(first));
    runner.AddGlobalTestEnvironment(std::move(second));

    ZX_ASSERT_MSG(runner.Run(Runner::kDefaultOptions) == 0, "Runner::Run encountered test errors.");

    ZX_ASSERT_MSG(first_ptr->set_up_order() < second_ptr->set_up_order(),
                  "Environment::SetUp is not following registration order.");
    ZX_ASSERT_MSG(first_ptr->tear_down_order() > second_ptr->tear_down_order(),
                  "Enironment::TearDown is not following reverse registration order.");
}

void RunnerRunOnlyFilteredTests() {
    Runner runner(Reporter(/*stream*/ nullptr));
    int test_counter = 0;
    int test_2_counter = 0;
    Runner::Options options = Runner::kDefaultOptions;
    options.filter = "TestCase.*";

    TestRef ref = runner.RegisterTest<Test, FakeTest>(
        kTestCaseName, kTestName, kFileName, kLineNumber, FakeTest::MakeFactory(&test_counter));
    TestRef ref2 = runner.RegisterTest<Test, FakeTest>(
        "TestCase2", kTestName, kFileName, kLineNumber, FakeTest::MakeFactory(&test_2_counter));

    ZX_ASSERT_MSG(ref.test_case_index != ref2.test_case_index,
                  "Different TestCase share same index.\n");

    // Verify that the runner actually claims to hold two tests from one test case.
    ZX_ASSERT_MSG(runner.summary().registered_test_count == 2,
                  "Test failed to register correctly.\n");
    ZX_ASSERT_MSG(runner.summary().registered_test_case_count == 2,
                  "TestCase failed to register correctly.\n");

    ZX_ASSERT_MSG(runner.Run(options) == 0, "Test Execution Failed.\n");

    // Check that the active count reflects a filter matching all.
    ZX_ASSERT_MSG(runner.summary().active_test_count == 1, "Failed to filter tests.\n");
    ZX_ASSERT_MSG(runner.summary().active_test_case_count == 1, "Failed to filter tests.\n");

    // Check that both tests were executed once.
    ZX_ASSERT_MSG(test_counter == 1, "test was filtered.\n");
    ZX_ASSERT_MSG(test_2_counter == 0, "test_2 was not filtered.\n");
}

void RunnerRunAllTestsSameTestCase() {
    Runner runner(Reporter(/*stream*/ nullptr));
    int test_counter = 0;
    int test_2_counter = 0;

    TestRef ref = runner.RegisterTest<Test, FakeTest>(
        kTestCaseName, kTestName, kFileName, kLineNumber, FakeTest::MakeFactory(&test_counter));
    TestRef ref2 = runner.RegisterTest<Test, FakeTest>(
        kTestCaseName, kTestName2, kFileName, kLineNumber, FakeTest::MakeFactory(&test_2_counter));

    ZX_ASSERT_MSG(ref.test_case_index == ref2.test_case_index, "Same TestCase share same index.\n");
    ZX_ASSERT_MSG(ref.test_index != ref2.test_index, "Different TestInfo share same index.\n");

    // Verify that the runner actually claims to hold two tests from one test case.
    ZX_ASSERT_MSG(runner.summary().registered_test_count == 2,
                  "Test failed to register correctly.\n");
    ZX_ASSERT_MSG(runner.summary().registered_test_case_count == 1,
                  "TestCase failed to register correctly.\n");

    ZX_ASSERT_MSG(runner.Run(Runner::kDefaultOptions) == 0, "Test Execution Failed.\n");

    // Check that the active count reflects a filter matching all.
    ZX_ASSERT_MSG(runner.summary().active_test_count == 2, "Failed to register both tests.\n");
    ZX_ASSERT_MSG(runner.summary().active_test_case_count == 1, "Failed to register both tests.\n");

    // Check that both tests were executed once.
    ZX_ASSERT_MSG(test_counter == 1, "test was not executed.\n");
    ZX_ASSERT_MSG(test_2_counter == 1, "test_2 was not executed.\n");
}

void RunnerRunReturnsNonZeroOnTestFailure() {
    Runner runner(Reporter(/*stream*/ nullptr));
    runner.RegisterTest<Test, FailingTest>(kTestCaseName, kTestName, kFileName, kLineNumber,
                                           FailingTest::MakeFactory(&runner));

    ZX_ASSERT_MSG(runner.Run(Runner::kDefaultOptions) != 0,
                  "Runner::Run must return non zero when at least one test fails.\n");
}

void RunnerListTests() {
    // Should produce the following output.
    constexpr char kExpectedOutput[] =
        "TestCase\n  .TestName\n  .TestName2\nTestCase2\n  .TestName\n  .TestName2\n";
    char buffer[100];
    memset(buffer, '\0', 100);
    FILE* memfile = fmemopen(buffer, 1024, "a");
    Runner runner((Reporter(/*stream*/ memfile)));

    // Register 2 testcases and 2 tests.
    runner.RegisterTest<Test, FakeTest>(kTestCaseName, kTestName, kFileName, kLineNumber);
    runner.RegisterTest<Test, FakeTest>(kTestCaseName, kTestName2, kFileName, kLineNumber);
    runner.RegisterTest<Test, FakeTest>(kTestCaseName2, kTestName, kFileName, kLineNumber);
    runner.RegisterTest<Test, FakeTest>(kTestCaseName2, kTestName2, kFileName, kLineNumber);

    runner.List(Runner::kDefaultOptions);
    fflush(memfile);
    ZX_ASSERT_MSG(strcmp(kExpectedOutput, buffer) == 0, "List output mismatch.");

    fclose(memfile);
}

void TestDriverImplReset() {
    TestDriverImpl driver;
    Assertion assertion("desc", "A", "A", "B", "B",
                        {.filename = kFileName, .line_number = kLineNumber},
                        /*is_fatal*/ true);

    driver.OnAssertion(assertion);
    ZX_ASSERT_MSG(!driver.Continue(),
                  "TestDriverImpl::Continue should return false after a fatal failure.\n");
    ZX_ASSERT_MSG(driver.HadAnyFailures(),
                  "TestDriverImpl::HadAnyFailures should return true after a fatal failure.\n");

    driver.Reset();

    ZX_ASSERT_MSG(driver.Continue(),
                  "TestDriverImpl::Continue should return true after TestDriverImpl::Reset.\n");
    ZX_ASSERT_MSG(
        driver.HadAnyFailures(),
        "TestDriverImpl::HadAnyFailures should not be affected by TestDriverImpl::Reset.\n");
}

void TestDriverImplFatalFailureEndsTest() {
    TestDriverImpl driver;
    Assertion assertion("desc", "A", "A", "B", "B",
                        {.filename = kFileName, .line_number = kLineNumber},
                        /*is_fatal*/ true);

    ZX_ASSERT_MSG(driver.Continue(), "TestDriverImpl::Continue should return true by default.\n");
    ZX_ASSERT_MSG(!driver.HadAnyFailures(),
                  "TestDriverImpl::HadAnyFailures should return false by default.\n");
    driver.OnAssertion(assertion);
    ZX_ASSERT_MSG(!driver.Continue(),
                  "TestDriverImpl::Continue should return false after a fatal failure.\n");
    ZX_ASSERT_MSG(driver.HadAnyFailures(),
                  "TestDriverImpl::HadAnyFailures should return true after a fatal failure.\n");
}

void TestDriverImplNonFatalFailureDoesNotEndTest() {
    TestDriverImpl driver;
    Assertion assertion("desc", "A", "A", "B", "B",
                        {.filename = kFileName, .line_number = kLineNumber},
                        /*is_fatal*/ false);

    ZX_ASSERT_MSG(driver.Continue(), "TestDriverImpl::Continue should return true by default.\n");
    ZX_ASSERT_MSG(!driver.HadAnyFailures(),
                  "TestDriverImpl::HadAnyFailures should return false by default.\n");
    driver.OnAssertion(assertion);
    ZX_ASSERT_MSG(driver.Continue(),
                  "TestDriverImpl::Continue should return false after a fatal failure.\n");
    ZX_ASSERT_MSG(driver.HadAnyFailures(),
                  "TestDriverImpl::HadAnyFailures should return true after a fatal failure.\n");
}

void TestDriverImplResetOnTestCompletion() {
    class FakeTest : public zxtest::Test {
    private:
        void TestBody() final {}
    };

    TestInfo test_info(kTestName, {.filename = kFileName, .line_number = kLineNumber},
                       &Test::Create<FakeTest>);
    TestCase test_case(kTestCaseName, Test::SetUpTestCase, Test::TearDownTestCase);
    struct CompleteFn {
        const char* name;
        void (TestDriverImpl::*complete)(const TestCase&, const TestInfo&);
    };
// Helper macro to generate appropiate error for each function.
#define CFN(fn)                                                                                    \
    { .name = #fn, .complete = &fn, }
    static constexpr CompleteFn complete_fns[] = {CFN(TestDriverImpl::OnTestSuccess),
                                                  CFN(TestDriverImpl::OnTestFailure),
                                                  CFN(TestDriverImpl::OnTestSkip)};
#undef CFN

    for (auto complete_fn : complete_fns) {
        TestDriverImpl driver;
        Assertion assertion("desc", "A", "A", "B", "B",
                            {.filename = kFileName, .line_number = kLineNumber},
                            /*is_fatal*/ false);

        driver.OnAssertion(assertion);
        (driver.*complete_fn.complete)(test_case, test_info);

        ZX_ASSERT_MSG(driver.Continue(), "%s should return true after test completion.\n",
                      complete_fn.name);
        ZX_ASSERT_MSG(driver.HadAnyFailures(), "%s should not reset on test completion.\n",
                      complete_fn.name);
    }
}

void RunnerOptionsParseFromCmdLineShort() {
    const char* kArgs[13] = {};
    kArgs[0] = "mybin";
    kArgs[1] = "-f";
    kArgs[2] = "+*:-ZxTest";
    kArgs[3] = "-i";
    kArgs[4] = "100";
    kArgs[5] = "-s";
    kArgs[6] = "-r";
    kArgs[7] = "10";
    kArgs[8] = "-l";
    kArgs[9] = "false";
    kArgs[10] = "-b";
    kArgs[11] = "-h";
    kArgs[12] = "true";

    fbl::Vector<fbl::String> errors;
    Runner::Options options =
        Runner::Options::FromArgs(countof(kArgs), const_cast<char**>(kArgs), &errors);

    // Just in case it returns errors, this will give insight into where the problem is.
    for (const auto& error : errors) {
        fprintf(stdout, "%s", error.c_str());
    }
    ZX_ASSERT_MSG(errors.is_empty(), "Runner::Options::FromArgs returned errors.\n");
    ZX_ASSERT_MSG(strcmp(options.filter.c_str(), kArgs[2]) == 0,
                  "Runner::Options::filter not parsed correctly.\n");
    ZX_ASSERT_MSG(options.repeat == 100, "Runner::Options::repeat not parsed correctly.\n");
    ZX_ASSERT_MSG(options.seed == 10, "Runner::Options::seed not parsed correctly.\n");
    ZX_ASSERT_MSG(options.shuffle, "Runner::Options::shuffle not parsed correctly.\n");
    ZX_ASSERT_MSG(options.list, "Runner::Options::list not parsed correctly.\n");
    ZX_ASSERT_MSG(!options.help, "Runner::Options::help not parsed correctly.\n");
    ZX_ASSERT_MSG(options.break_on_failure,
                  "Runner::Options::break_on_failure not parsed correctly.\n");
}

void RunnerOptionsParseFromCmdLineLong() {
    const char* kArgs[13] = {};
    kArgs[0] = "mybin";
    kArgs[1] = "--gtest_filter";
    kArgs[2] = "+*:-ZxTest";
    kArgs[3] = "--gtest_repeat";
    kArgs[4] = "100";
    kArgs[5] = "--gtest_shuffle";
    kArgs[6] = "--gtest_random_seed";
    kArgs[7] = "10";
    kArgs[8] = "--gtest_list_tests";
    kArgs[9] = "false";
    kArgs[10] = "--gtest_break_on_failure";
    kArgs[11] = "--help";
    kArgs[12] = "true";

    fbl::Vector<fbl::String> errors;
    Runner::Options options =
        Runner::Options::FromArgs(countof(kArgs), const_cast<char**>(kArgs), &errors);

    // Just in case it returns errors, this will give insight into where the problem is.
    for (const auto& error : errors) {
        fprintf(stdout, "%s", error.c_str());
    }
    ZX_ASSERT_MSG(errors.is_empty(), "Runner::Options::FromArgs returned errors.\n.");
    ZX_ASSERT_MSG(strcmp(options.filter.c_str(), kArgs[2]) == 0,
                  "Runner::Options::filter not parsed correctly\n.");
    ZX_ASSERT_MSG(options.repeat == 100, "Runner::Options::repeat not parsed correctly\n.");
    ZX_ASSERT_MSG(options.seed == 10, "Runner::Options::seed not parsed correctly\n.");
    ZX_ASSERT_MSG(options.shuffle, "Runner::Options::shuffle not parsed correctly\n.");
    ZX_ASSERT_MSG(options.list, "Runner::Options::list not parsed correctly\n.");
    ZX_ASSERT_MSG(!options.help, "Runner::Options::help not parsed correctly\n.");
    ZX_ASSERT_MSG(options.break_on_failure,
                  "Runner::Options::break_on_failure not parsed correctly.\n");
}

void RunnerOptionsParseFromCmdLineErrors() {
    const char* kArgs[3] = {};
    kArgs[0] = "mybin";
    kArgs[1] = "--gtest_repeat";
    kArgs[2] = "-2";

    fbl::Vector<fbl::String> errors;
    Runner::Options options =
        Runner::Options::FromArgs(countof(kArgs), const_cast<char**>(kArgs), &errors);

    // Just in case it returns errors, this will give insight into where the problem is.
    ZX_ASSERT_MSG(!errors.is_empty(), "Runner::Options::FromArgs should return error.\n.");
}

void FilterOpFilterEmptyMatchesAll() {
    constexpr char kPattern[] = "";
    FilterOp filter;
    filter.pattern = kPattern;

    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName), "FilterOp failed to recognize a full match.");
    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName2), "FilterOp failed to recognize a mismatch.");
}

void FilterOpFilterFullMatch() {
    constexpr char kPattern[] = "TestCase.TestName";
    FilterOp filter;
    filter.pattern = kPattern;

    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName), "FilterOp failed to recognize a full match.");
    ZX_ASSERT_MSG(!filter(kTestCaseName, kTestName2), "FilterOp failed to recognize a mismatch.");
}

void FilterOpFilterFullNegativeMatch() {
    constexpr char kPattern[] = "-TestCase.TestName";
    FilterOp filter;
    filter.pattern = kPattern;

    ZX_ASSERT_MSG(!filter(kTestCaseName, kTestName),
                  "FilterOp failed to recognize a full negative match.");
    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName2),
                  "FilterOp failed to recognize a negative mismatch.");
}

void FilterOpFilterPartialMatch() {
    constexpr char kPattern[] = "TestCase.TestName*";
    FilterOp filter;
    filter.pattern = kPattern;

    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName),
                  "FilterOp failed to recognize a partial match.");
    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName2),
                  "FilterOp failed to recognize a partial match.");
}

void FilterOpFilterMultiMatch() {
    constexpr char kPattern[] = "TestCase.TestName:TestCase.TestName2";
    FilterOp filter;
    filter.pattern = kPattern;

    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName),
                  "FilterOp failed to recognize first of multiple patterns.");
    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName2),
                  "FilterOp failed to recognize second of multiple patterns.");
}

void FilterOpFilterCombined() {
    constexpr char kPattern[] = "TestCase.TestName:-TestCase.TestName2";
    FilterOp filter;
    filter.pattern = kPattern;

    ZX_ASSERT_MSG(filter(kTestCaseName, kTestName),
                  "FilterOp failed to recognize first of multiple patterns.");
    ZX_ASSERT_MSG(!filter(kTestCaseName, kTestName2),
                  "FilterOp failed to recognize second of multiple patterns.");
}

} // namespace test
} // namespace zxtest
