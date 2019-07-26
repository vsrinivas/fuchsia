// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

#include <memory>

#include <zircon/assert.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test-internal.h>

namespace zxtest {

using internal::TestDriver;
using internal::TestInternal;
using internal::TestStatus;
namespace test {
namespace {

// Meant to make template instantiation more readable.
constexpr bool kPassSetUp = false;
constexpr bool kFailsSetUp = true;
constexpr bool kPassTestBody = false;
constexpr bool kFailsTestBody = true;

template <bool FailOnSetUp, bool FailOnTestBody>
class FakeTest : public zxtest::Test {
 public:
  void SetUp() final {
    if (FailOnSetUp) {
      driver->NotifyFail();
    }
    run_setup = true;
  }

  void TearDown() final { run_teardown = true; }

  void TestBody() final {
    if (FailOnTestBody) {
      driver->NotifyFail();
    }
    run_body = true;
  }

  // Used for verying that Run behaves properly.
  bool run_setup = false;
  bool run_teardown = false;
  bool run_body = false;
  TestDriverStub* driver = nullptr;
};

}  // namespace

void TestRun() {
  TestDriverStub driver;
  auto test = Test::Create<FakeTest<kPassSetUp, kPassTestBody>>(&driver);
  test->driver = &driver;

  test->Run();
  ZX_ASSERT_MSG(test->run_setup, "Test did not execute SetUp");
  ZX_ASSERT_MSG(test->run_body, "Test did not execute TestBody");
  ZX_ASSERT_MSG(test->run_teardown, "Test did not execute TearDown");
}

void TestRunFailure() {
  TestDriverStub driver;
  auto test = Test::Create<FakeTest<kPassSetUp, kFailsTestBody>>(&driver);
  test->driver = &driver;

  test->Run();
  ZX_ASSERT_MSG(test->run_setup, "Test did not execute SetUp");
  ZX_ASSERT_MSG(test->run_body, "Test did not execute TestBody");
  ZX_ASSERT_MSG(test->run_teardown, "Test did not execute TearDown");
}

void TestSetUpFailure() {
  TestDriverStub driver;
  auto test = Test::Create<FakeTest<kFailsSetUp, kFailsTestBody>>(&driver);
  test->driver = &driver;

  test->Run();
  ZX_ASSERT_MSG(test->run_setup, "Test did not execute SetUp");
  ZX_ASSERT_MSG(!test->run_body, "Test did execute TestBody when its SetUp failed.");
  ZX_ASSERT_MSG(test->run_teardown, "Test did not execute TearDown");
}

}  // namespace test
}  // namespace zxtest
