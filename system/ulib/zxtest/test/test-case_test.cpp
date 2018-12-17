// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

#include <fbl/function.h>
#include <zircon/assert.h>
#include <zxtest/base/test-case.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test.h>

namespace zxtest {
using internal::SourceLocation;
using internal::TestCase;
using internal::TestDriver;
using internal::TestInfo;
namespace test {
namespace {

constexpr char kTestCaseName[] = "TestCase";

void Stub() {}

class FakeTest : public zxtest::Test {
public:
    fbl::Function<void()> body = &Stub;

private:
    void TestBody() final { body(); }
};

} // namespace

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

    ZX_ASSERT_MSG(test_case.TestCount() == 1, "TestCase matching count does not match.");
    ZX_ASSERT_MSG(test_case.MatchingTestCount() == 1, "TestCase .");
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
    test_case.Run(&driver);

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
                                             test->body = [&run_order]() {
                                                 run_order.push_back(1);
                                             };
                                             return test;
                                         }),
                  "TestCase failed to register a test.");

    ZX_ASSERT_MSG(test_case.RegisterTest("TestName2", kLocation,
                                         [&run_order](TestDriver* driver) {
                                             auto test = Test::Create<FakeTest>(driver);
                                             test->body = [&run_order]() {
                                                 run_order.push_back(2);
                                             };
                                             return test;
                                         }),
                  "TestCase failed to register a test.");

    ZX_ASSERT_MSG(test_case.RegisterTest("TestName3", kLocation,
                                         [&run_order](TestDriver* driver) {
                                             auto test = Test::Create<FakeTest>(driver);
                                             test->body = [&run_order]() {
                                                 run_order.push_back(3);
                                             };
                                             return test;
                                         }),
                  "TestCase failed to register a test.");

    // With seed = 0 and 3 tests, using musl implementation of random , we get 2 3 1 run order.
    test_case.Shuffle(0);
    test_case.Run(&driver);

    ZX_ASSERT_MSG(run_order[0] == 2, "Shuffle failed.");
    ZX_ASSERT_MSG(run_order[1] == 3, "Shuffle failed.");
    ZX_ASSERT_MSG(run_order[2] == 1, "Shuffle failed.");
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
                                             test->body = [&run_order]() {
                                                 run_order.push_back(1);
                                             };
                                             return test;
                                         }),
                  "TestCase failed to register a test.");

    ZX_ASSERT_MSG(test_case.RegisterTest("TestName2", kLocation,
                                         [&run_order](TestDriver* driver) {
                                             auto test = Test::Create<FakeTest>(driver);
                                             test->body = [&run_order]() {
                                                 run_order.push_back(2);
                                             };
                                             return test;
                                         }),
                  "TestCase failed to register a test.");

    ZX_ASSERT_MSG(test_case.RegisterTest("TestName3", kLocation,
                                         [&run_order](TestDriver* driver) {
                                             auto test = Test::Create<FakeTest>(driver);
                                             test->body = [&run_order]() {
                                                 run_order.push_back(3);
                                             };
                                             return test;
                                         }),
                  "TestCase failed to register a test.");

    test_case.Shuffle(0);
    test_case.UnShuffle();
    test_case.Run(&driver);

    ZX_ASSERT_MSG(run_order[0] == 1, "UnShuffle failed.");
    ZX_ASSERT_MSG(run_order[1] == 2, "UnShuffle failed.");
    ZX_ASSERT_MSG(run_order[2] == 3, "UNShuffle failed.");
}

} // namespace test
} // namespace zxtest
