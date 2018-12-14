// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
class TestDriverStub : public internal::TestDriver {
public:
    ~TestDriverStub() final{};

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

struct RegisteredTest {
    const char* name = nullptr;
    void (*test_fn)() = nullptr;
};

// Just so we capture the function name.
#define RUN_TEST(test_function)                                                                    \
    RegisteredTest { .name = #test_function, .test_fn = &test_function }

// List of tests to run.
static constexpr RegisteredTest kRegisteredTests[] = {
    RUN_TEST(TestRun),         RUN_TEST(TestRunFailure),      RUN_TEST(TestSetUpFailure),
    RUN_TEST(TestInfoDefault), RUN_TEST(TestInfoInstantiate),
};

#undef RUN_TEST

} // namespace test
} // namespace zxtest
