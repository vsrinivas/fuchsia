// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_OBSERVER_H_
#define ZXTEST_BASE_OBSERVER_H_

namespace zxtest {
// Forward declaration.
class Assertion;
class Runner;
class TestCase;
class TestInfo;

// Allows user to listen for lifecycle events. This allows injecting code at specific
// instants, for example when there is a global set up and tear down for a library,
// that is done at process start up.
// This interface mimicks gTest EventObserver, all methods are stubbed with empty body,
// so implementing classes, only override those they are interested in.
//
// Note: This interface will be expanded incrementally in a series of patches,
// so it becomes easier to review.
class LifecycleObserver {
 public:
  virtual ~LifecycleObserver() = default;

  // Reports before any test is executed.
  virtual void OnProgramStart(const Runner& runner) {
  }

  // Reports before every iteration starts.
  virtual void OnIterationStart(const Runner& runner, int iteration) {
  }

  // Reports before any environment is set up.
  virtual void OnEnvironmentSetUp(const Runner& runner) {
  }

  // Reports before a TestCase is set up.
  virtual void OnTestCaseStart(const TestCase& test_case) {
  }

  // Reports before a test starts.
  virtual void OnTestStart(const TestCase& test_case, const TestInfo& test) {
  }

  // Reports when an assertion on the running tests fails.
  virtual void OnAssertion(const Assertion& assertion) {
  }

  // Reports after a test execution was skipped.
  virtual void OnTestSkip(const TestCase& test_case, const TestInfo& test) {
  }

  // Reports after test execution completed with failures.
  virtual void OnTestFailure(const TestCase& test_case, const TestInfo& test) {
  }

  // Reports after test execution completed with no failures.
  virtual void OnTestSuccess(const TestCase& test_case, const TestInfo& test) {
  }

  // Reports before a TestCase is torn down.
  virtual void OnTestCaseEnd(const TestCase& test_case) {
  }

  // Reports before any environment is torn down.
  virtual void OnEnvironmentTearDown(const Runner& runner) {
  }

  // Reports before every iteration starts.
  virtual void OnIterationEnd(const Runner& runner, int iteration) {
  }

  // Reports after all test executed.
  virtual void OnProgramEnd(const Runner& runner) {
  }
};

}  // namespace zxtest

#endif  // ZXTEST_BASE_OBSERVER_H_
