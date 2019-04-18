// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <sdk/lib/sys/cpp/service_directory.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>

namespace modular {
namespace testing {

// A gtest fixture for tests that require an instance of the modular runtime.
// This fixture requires the `modular_test_harness` package to be available.
class TestHarnessFixture : public sys::testing::TestWithEnvironment {
 protected:
  TestHarnessFixture();
  virtual ~TestHarnessFixture();

  fuchsia::modular::testing::TestHarnessPtr& test_harness() {
    return test_harness_;
  }

 private:
  std::shared_ptr<sys::ServiceDirectory> svc_;
  fuchsia::modular::testing::TestHarnessPtr test_harness_;
  fuchsia::sys::ComponentControllerPtr test_harness_ctrl_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_
