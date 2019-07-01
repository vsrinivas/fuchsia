// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

#include <cstring>

#include <fbl/function.h>
#include <zircon/assert.h>
#include <zxtest/base/observer.h>
#include <zxtest/base/test-case.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test.h>

namespace zxtest {
using internal::TestDriver;
namespace test {
namespace {

constexpr char kTestCaseName[] = "TestCase";

void Stub() {
}

class FakeTest : public zxtest::Test {
 public:
  fbl::Function<void()> body = &Stub;

 private:
  void TestBody() final {
    body();
  }
};

// Lifecycle observer that verifies that callbacks are executed correctly within
// zxtest::TestCase.
class FakeLifecycleObserver : public LifecycleObserver {
 public:
  ~FakeLifecycleObserver() final {
  }

  // Reports before every TestCase is set up.
  void OnTestCaseStart(const TestCase& test_case) final {
    ZX_ASSERT_MSG(test_case_++ == 0 && test_ == 0,
                  "LifecycleObserver::TestCaseStart was not called before any test execution.\n");
  }

  // Reports before every test starts.
  void OnTestStart(const TestCase& test_case, const TestInfo& test) final {
    ZX_ASSERT_MSG(test_++ == 0, "LifecycleObserver::TestStart was not called second.\n");
  }

  // Reports before every test starts.
  void OnTestSkip(const TestCase& test_case, const TestInfo& test) final {
    ZX_ASSERT_MSG(test_ == 1, "LifecycleObserver::TestSkip was not called third.\n");
    test_ = 0;
  }

  // Reports before every TestCase is set up.
  void OnTestFailure(const TestCase& test_case, const TestInfo& test) final {
    ZX_ASSERT_MSG(test_ == 1, "LifecycleObserver::TestFailure was not called third.\n");
    test_ = 0;
  }

  // Reports before every TestCase is set up.
  void OnTestSuccess(const TestCase& test_case, const TestInfo& test) final {
    ZX_ASSERT_MSG(test_ == 1, "LifecycleObserver::TestSuccess was not called third.\n");
    test_ = 0;
  }

  // Reports before every TestCase is torn down.
  void OnTestCaseEnd(const TestCase& test_case) final {
    ZX_ASSERT_MSG(test_case_ == 1 && test_ == 0,
                  "LifecycleObserver::TestCaseEnd was not called after all tests.\n");
    test_case_ = 0;
  }

 private:
  size_t test_case_ = 0;
  size_t test_ = 0;
};

}  // namespace

void TestCaseDefault() {
  TestCase test_case(kTestCaseName, &Stub, &Stub);

  ZX_ASSERT_MSG(test_case.name() == kTestCaseName, "TestCase name do no match.");
  ZX_ASSERT_MSG(test_case.TestCount() == 0, "TestCase is not initialized with 0 tests.");
  ZX_ASSERT_MSG(test_case.MatchingTestCount() == 0,
                "TestCase is not initialized with 0 matching tests.");
}

void TestCaseRegisterTest() {
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register a test.");
  const TestInfo& registered_test = test_case.GetTestInfo(/*index*/ 0);

  ZX_ASSERT_MSG(test_case.TestCount() == 1, "TestCase test count does not match.");
  ZX_ASSERT_MSG(test_case.MatchingTestCount() == 1, "TestCase matching test count does not match.");
  ZX_ASSERT_MSG(registered_test.name() == kTestName,
                "TestCase expected TestInfo name is incorrect.");
  ZX_ASSERT_MSG(memcmp(&registered_test.location(), &kLocation, sizeof(SourceLocation)) == 0,
                "TestCase expected TestInfo name is incorrect.");
}

void TestCaseRun() {
  TestDriverStub driver;
  int order = 0;
  int set_up;
  int tear_down;
  int test;
  TestCase test_case(
      kTestCaseName, [&order, &set_up]() { set_up = order++; },
      [&order, &tear_down]() { tear_down = order++; });
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation,
                                       [&order, &test](TestDriver* driver) {
                                         auto test_ptr = Test::Create<FakeTest>(driver);
                                         test_ptr->body = [&order, &test]() { test = order++; };
                                         return test_ptr;
                                       }),
                "TestCase failed to register a test.");
  FakeLifecycleObserver observer;
  test_case.Run(&observer, &driver);

  ZX_ASSERT_MSG(set_up < test, "Test executed before Test::SetUpTestCase\n");
  ZX_ASSERT_MSG(test < tear_down, "Test::TearDownTestCase executed before Test/\n ");
}

void TestCaseRegisterDuplicatedTestFails() {
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register a test.");
  // Registering a test with the same name, will fail.
  ZX_ASSERT_MSG(!test_case.RegisterTest(kTestName, kLocation, &Test::Create<FakeTest>),
                "TestCase failed to detect duplicated test.");

  ZX_ASSERT_MSG(test_case.TestCount() == 1, "TestCase::TestCount does not match expected value.");
  ZX_ASSERT_MSG(test_case.MatchingTestCount() == 1,
                "TestCase::MatchingTestCount does not match expected value.");
}

void TestCaseFilter() {
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName2", kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register second test.");

  test_case.Filter([&kTestName](const fbl::String& test_case, const fbl::String& test) {
    return test == kTestName;
  });

  ZX_ASSERT_MSG(test_case.TestCount() == 2, "TestCase::TestCount does not match expected value.");
  ZX_ASSERT_MSG(test_case.MatchingTestCount() == 1,
                "TestCase::MatchingTestCount does not match expected value.");
}

void TestCaseFilterNoMatches() {
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";

  ZX_ASSERT_MSG(
      test_case.RegisterTest(kTestName, kLocation,
                             [](TestDriver* driver) { return std::make_unique<FakeTest>(); }),
      "TestCase failed to register a test.");

  test_case.Filter([](const fbl::String& test_case, const fbl::String& test) { return false; });

  ZX_ASSERT_MSG(test_case.TestCount() == 1, "TestCase::TestCount does not match expected value.");
  ZX_ASSERT_MSG(test_case.MatchingTestCount() == 0,
                "TestCase::MatchingTestCount does not match expected value.");
}

void TestCaseFilterAllMatching() {
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName2", kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register a test.");

  test_case.Filter([](const fbl::String& test_case, const fbl::String& test) { return true; });

  ZX_ASSERT_MSG(test_case.TestCount() == 2, "TestCase::TestCount does not match expected value.");
  ZX_ASSERT_MSG(test_case.MatchingTestCount() == 2,
                "TestCase::MatchingTestCount does not match expected value.");
}

void TestCaseFilterNullMatchesAll() {
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName2", kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register a test.");

  test_case.Filter(nullptr);

  ZX_ASSERT_MSG(test_case.TestCount() == 2, "TestCase::TestCount does not match expected value.");
  ZX_ASSERT_MSG(test_case.MatchingTestCount() == 2,
                "TestCase::MatchingTestCount does not match expected value.");
}

void TestCaseFilterDoNotAccumulate() {
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation, &Test::Create<FakeTest>),
                "TestCase failed to register a test.");

  test_case.Filter([](const fbl::String& test_case, const fbl::String& test) { return false; });
  test_case.Filter([](const fbl::String& test_case, const fbl::String& test) { return true; });

  ZX_ASSERT_MSG(test_case.TestCount() == 1, "TestCase::TestCount does not match expected value.");
  ZX_ASSERT_MSG(test_case.MatchingTestCount() == 1,
                "TestCase::MatchingTestCount does not match expected value.");
}

void TestCaseShuffle() {
  TestDriverStub driver;
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";
  fbl::Vector<int> run_order;

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation,
                                       [&run_order](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = [&run_order]() { run_order.push_back(1); };
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName2", kLocation,
                                       [&run_order](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = [&run_order]() { run_order.push_back(2); };
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName3", kLocation,
                                       [&run_order](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = [&run_order]() { run_order.push_back(3); };
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  test_case.Filter(nullptr);

  // With seed = 0 and 3 tests, using musl implementation of random , we get 2 3 1 run order.
  LifecycleObserver observer;
  test_case.Shuffle(3);
  test_case.Run(&observer, &driver);

  test_case.UnShuffle();
  test_case.Shuffle(3);
  test_case.Run(&observer, &driver);

  // Same seed same order.
  ZX_ASSERT_MSG(run_order[0] == run_order[3], "Shuffle failed.");
  ZX_ASSERT_MSG(run_order[1] == run_order[4], "Shuffle failed.");
  ZX_ASSERT_MSG(run_order[2] == run_order[5], "Shuffle failed.");

  test_case.UnShuffle();
  test_case.Shuffle(5);
  test_case.Run(&observer, &driver);

  // Different seeds different order.
  ZX_ASSERT_MSG(
      run_order[6] != run_order[3] || run_order[7] != run_order[4] || run_order[8] != run_order[5],
      "Shuffle failed.");
}

void TestCaseUnShuffle() {
  TestDriverStub driver;
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";
  fbl::Vector<int> run_order;

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation,
                                       [&run_order](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = [&run_order]() { run_order.push_back(1); };
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName2", kLocation,
                                       [&run_order](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = [&run_order]() { run_order.push_back(2); };
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName3", kLocation,
                                       [&run_order](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = [&run_order]() { run_order.push_back(3); };
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  LifecycleObserver observer;
  test_case.Shuffle(0);
  test_case.UnShuffle();
  test_case.Run(&observer, &driver);

  ZX_ASSERT_MSG(run_order[0] == 1, "UnShuffle failed.");
  ZX_ASSERT_MSG(run_order[1] == 2, "UnShuffle failed.");
  ZX_ASSERT_MSG(run_order[2] == 3, "UNShuffle failed.");
}

void TestCaseRunUntilFailure() {
  TestDriverStub stub_driver;
  TestCase test_case(kTestCaseName, &Stub, &Stub);
  const SourceLocation kLocation = {.filename = "test.cpp", .line_number = 1};
  const fbl::String kTestName = "TestName";
  bool third_test_executed = false;

  ZX_ASSERT_MSG(test_case.RegisterTest(kTestName, kLocation,
                                       [](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = []() {};
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName2", kLocation,
                                       [&stub_driver](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = [&stub_driver]() {
                                           stub_driver.NotifyFail();
                                         };
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  ZX_ASSERT_MSG(test_case.RegisterTest("TestName3", kLocation,
                                       [&third_test_executed](TestDriver* driver) {
                                         auto test = Test::Create<FakeTest>(driver);
                                         test->body = [&third_test_executed]() {
                                           third_test_executed = true;
                                         };
                                         return test;
                                       }),
                "TestCase failed to register a test.");

  test_case.Filter(nullptr);

  // With seed = 0 and 3 tests, using musl implementation of random , we get 2 3 1 run order.
  LifecycleObserver observer;
  test_case.SetReturnOnFailure(true);
  test_case.Run(&observer, &stub_driver);

  ZX_ASSERT_MSG(!third_test_executed,
                "TestCase::SetReturnOnFailure did not return on first test case failure.");
}

}  // namespace test
}  // namespace zxtest
