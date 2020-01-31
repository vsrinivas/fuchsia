// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZXTEST_TEST_TEST_REGISTRY_H_
#define ZIRCON_SYSTEM_ULIB_ZXTEST_TEST_TEST_REGISTRY_H_

#include <zxtest/base/test-driver.h>
#include <zxtest/base/test.h>

// Because this library defines a testing framework we cannot rely on it
// to correctly run our tests. Testing this library is done by manually
// adding functions into this header file and calling them in main.
//
// Assertions mechanisms are also unreliable, so use ZX_ASSERT instead.
// You should assume zxtest is not working when adding a test.
namespace zxtest {
namespace test {

// Stub used for testing;
class TestDriverStub final : public internal::TestDriver {
 public:
  ~TestDriverStub() final {}

  void Skip() final {}

  bool Continue() const final { return should_continue_; }

  void NotifyFail() { should_continue_ = false; }

  internal::TestStatus Status() const final { return internal::TestStatus::kFailed; }

 private:
  bool should_continue_ = true;
};

// Verify that without errors, |Test::TestBody| is called after |Test::SetUp| and
// before |Test::TearDown|.
void TestRun();

// Verify that on |Test::Run| error |Test::TearDown| is still called.
void TestRunFailure();

// Verify that on |Test::SetUp| failure |Test::TearDown| is still called, but
// |Test::Run| is ignored.
void TestSetUpFailure();

// Verify that |TestInfo| construction is working as expecting.
void TestInfoDefault();

// Verify that the instantiated |std::unique_ptr<Test>| is actually from the provided factory.
void TestInfoInstantiate();

// Verify default state of a |TestCase.
void TestCaseDefault();

// Verify that |TestCase::RegisterTest| adds test correctly.
void TestCaseRegisterTest();

// Verify that |TestCase::RegisterTest| returns false for duplicated tests names.
void TestCaseRegisterDuplicatedTestFails();

// Verify that |Testcase::Run| executes TestCase set up and tear down in the correct order.
void TestCaseRun();
void TestCaseRunUntilFailure();

// Verify that |TestCase::Filter| works as expected.
void TestCaseFilter();
void TestCaseFilterNoMatches();
void TestCaseFilterAllMatching();
void TestCaseFilterNullMatchesAll();

// Verify that |TestCase::Filter| is idempotent.
void TestCaseFilterDoNotAccumulate();

// Verify that |TestCase::Shuffle| changes the execution order.
void TestCaseShuffle();

// Verify that |TestCase::UnShuffle| restores original order.
void TestCaseUnShuffle();

// Verify that |Assertion::Assertion(...)| generate the right values.
void AssertionHasValues();
void AssertionHasNoValues();

// Verify that the broadcasting is working for all LifecycleObserver
// events.
void EventBroadcasterOnProgramStart();
void EventBroadcasterOnIterationStart();
void EventBroadcasterOnEnvironmentSetUp();
void EventBroadcasterOnTestCaseStart();
void EventBroadcasterOnTestStart();
void EventBroadcasterOnAssertion();
void EventBroadcasterOnTestSkip();
void EventBroadcasterOnTestSuccess();
void EventBroadcasterOnTestFailure();
void EventBroadcasterOnTestCaseEnd();
void EventBroadcasterOnEnvironmentTearDown();
void EventBroadcasterOnIterationEnd();
void EventBroadcasterOnProgramEnd();

// Verify FileLogSink behavior since it is the default log sink.
void FileLogSinkWrite();
void FileLogSinkCallCloserOnDestruction();

// Verify that Runner behaves appropiately with the defined options.
void RunnerRegisterTest();
void RunnerRegisterTestWithCustomFactory();
void RunnerLifecycleObserversRegisteredAndNotified();
void RunnerRunAllTests();
void RunnerRunAllTestsUntilFailure();
void RunnerRunAllTestsSameTestCase();
void RunnerSetUpAndTearDownEnvironmentsTests();
void RunnerRunOnlyFilteredTests();
void RunnerRepeatTests();
void RunnerRunReturnsNonZeroOnTestFailure();
void RunnerRunReturnsZeroOnAssertionsDisabled();
void RunnerRunReturnsNonZeroOnAssertionsReEnabled();
void RunnerListTests();

// Verify that TestDriverImpl actually resets on the right spots,
// and behaves correctly on fatal and non fatal failure(EXPECT_*, ASSERT_*).
void TestDriverImplFatalFailureEndsTest();
void TestDriverImplNonFatalFailureDoesNotEndTest();
void TestDriverImplResetOnTestCompletion();
void TestDriverImplReset();

// Verify that we parse options correctly.
void RunnerOptionsParseFromCmdLineShort();
void RunnerOptionsParseFromCmdLineLong();
void RunnerOptionsParseFromCmdLineErrors();

// Verify that swapping the reporter actually changes where things are outputted.
void ReporterWritesToLogSink();
void ReporterSetLogSink();

// Verify that the current Filter implementation matches gTest expectations.
void FilterOpFilterEmptyMatchesAll();
void FilterOpFilterFullMatch();
void FilterOpFilterPartialMatch();
void FilterOpFilterFullNegativeMatch();
void FilterOpFilterMultiMatch();
void FilterOpFilterCombined();

// Fuchsia only tests.
#ifdef __Fuchsia__
void DeathStatementCrash();
void DeathStatementNoCrash();
void DeathStatementInternalError();
#endif

struct RegisteredTest {
  const char* name = nullptr;
  void (*test_fn)() = nullptr;
};

// Just so we capture the function name.
#define RUN_TEST(test_function) \
  RegisteredTest { .name = #test_function, .test_fn = &test_function }

// List of tests to run.
static constexpr RegisteredTest kRegisteredTests[] = {
    RUN_TEST(TestRun),
    RUN_TEST(TestRunFailure),
    RUN_TEST(TestSetUpFailure),
    RUN_TEST(TestInfoDefault),
    RUN_TEST(TestInfoInstantiate),
    RUN_TEST(TestCaseDefault),
    RUN_TEST(TestCaseRegisterTest),
    RUN_TEST(TestCaseRegisterDuplicatedTestFails),
    RUN_TEST(TestCaseRun),
    RUN_TEST(TestCaseRunUntilFailure),
    RUN_TEST(TestCaseFilter),
    RUN_TEST(TestCaseFilterNoMatches),
    RUN_TEST(TestCaseFilterAllMatching),
    RUN_TEST(TestCaseFilterNullMatchesAll),
    RUN_TEST(TestCaseFilterDoNotAccumulate),
    RUN_TEST(TestCaseShuffle),
    RUN_TEST(TestCaseUnShuffle),
    RUN_TEST(AssertionHasValues),
    RUN_TEST(AssertionHasNoValues),
    RUN_TEST(EventBroadcasterOnProgramStart),
    RUN_TEST(EventBroadcasterOnIterationStart),
    RUN_TEST(EventBroadcasterOnEnvironmentSetUp),
    RUN_TEST(EventBroadcasterOnTestCaseStart),
    RUN_TEST(EventBroadcasterOnTestStart),
    RUN_TEST(EventBroadcasterOnAssertion),
    RUN_TEST(EventBroadcasterOnTestSkip),
    RUN_TEST(EventBroadcasterOnTestSuccess),
    RUN_TEST(EventBroadcasterOnTestFailure),
    RUN_TEST(EventBroadcasterOnTestCaseEnd),
    RUN_TEST(EventBroadcasterOnEnvironmentTearDown),
    RUN_TEST(EventBroadcasterOnIterationEnd),
    RUN_TEST(EventBroadcasterOnProgramEnd),
    RUN_TEST(FileLogSinkWrite),
    RUN_TEST(FileLogSinkCallCloserOnDestruction),
    RUN_TEST(RunnerRegisterTest),
    RUN_TEST(RunnerRegisterTestWithCustomFactory),
    RUN_TEST(RunnerLifecycleObserversRegisteredAndNotified),
    RUN_TEST(RunnerRunAllTests),
    RUN_TEST(RunnerRunAllTestsUntilFailure),
    RUN_TEST(RunnerRunAllTestsSameTestCase),
    RUN_TEST(RunnerRunReturnsNonZeroOnTestFailure),
    RUN_TEST(RunnerRunReturnsZeroOnAssertionsDisabled),
    RUN_TEST(RunnerRunReturnsNonZeroOnAssertionsReEnabled),
    RUN_TEST(RunnerSetUpAndTearDownEnvironmentsTests),
    RUN_TEST(RunnerRunOnlyFilteredTests),
    RUN_TEST(RunnerListTests),
    RUN_TEST(ReporterSetLogSink),
    RUN_TEST(ReporterWritesToLogSink),
    RUN_TEST(TestDriverImplFatalFailureEndsTest),
    RUN_TEST(TestDriverImplNonFatalFailureDoesNotEndTest),
    RUN_TEST(TestDriverImplReset),
    RUN_TEST(TestDriverImplResetOnTestCompletion),
    RUN_TEST(RunnerOptionsParseFromCmdLineShort),
    RUN_TEST(RunnerOptionsParseFromCmdLineLong),
    RUN_TEST(RunnerOptionsParseFromCmdLineErrors),
    RUN_TEST(FilterOpFilterEmptyMatchesAll),
    RUN_TEST(FilterOpFilterFullMatch),
    RUN_TEST(FilterOpFilterFullNegativeMatch),
    RUN_TEST(FilterOpFilterPartialMatch),
    RUN_TEST(FilterOpFilterMultiMatch),
    RUN_TEST(FilterOpFilterCombined),
#ifdef __Fuchsia__
    RUN_TEST(DeathStatementCrash),
    RUN_TEST(DeathStatementNoCrash),
    RUN_TEST(DeathStatementInternalError),
#endif
};

#undef RUN_TEST

}  // namespace test
}  // namespace zxtest

#endif  // ZIRCON_SYSTEM_ULIB_ZXTEST_TEST_TEST_REGISTRY_H_
