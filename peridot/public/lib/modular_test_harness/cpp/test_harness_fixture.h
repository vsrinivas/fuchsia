// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

namespace modular {
namespace testing {

// A gtest fixture for tests that require an instance of the modular runtime.
// This fixture depends on the `modular_test_harness` fuchsia package.
class TestHarnessFixture : public sys::testing::TestWithEnvironment {
 protected:
  TestHarnessFixture();
  virtual ~TestHarnessFixture();

  fuchsia::modular::testing::TestHarnessPtr& test_harness() {
    return test_harness_;
  }

  // Configure |test_harness| to intercept to base shell. Returns the generated
  // fake URL used to configure the base shell.
  std::string InterceptBaseShell(
      fuchsia::modular::testing::TestHarnessSpec* spec) const;

  // Configure |test_harness| with a new session shell, and set it up for
  // interception. Returns the generated fake URL used to configure the session
  // shell.
  std::string InterceptSessionShell(
      fuchsia::modular::testing::TestHarnessSpec* spec) const;

  // Configure |test_harness| to intercept to story shell. Returns the generated
  // fake URL used to configure the story shell.
  std::string InterceptStoryShell(
      fuchsia::modular::testing::TestHarnessSpec* spec) const;

  // Returns a generated fake URL. Subsequent calls to this method will generate
  // a different URL.
  std::string GenerateFakeUrl() const;

 private:
  std::shared_ptr<sys::ServiceDirectory> svc_;
  fuchsia::modular::testing::TestHarnessPtr test_harness_;
  fuchsia::sys::ComponentControllerPtr test_harness_ctrl_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_
