// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TESTING_CPP_TEST_HARNESS_LAUNCHER_H_
#define LIB_MODULAR_TESTING_CPP_TEST_HARNESS_LAUNCHER_H_

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <condition_variable>
#include <thread>

namespace modular {
namespace testing {

// TestHarnessLauncher launches and manages an instance of the modular test
// harness component. Use this class to acquire an instance of the
// |fuchsia.modular.TestHarness| service.
class TestHarnessLauncher final {
 public:
  // Launches the modular test harness component.
  explicit TestHarnessLauncher(fuchsia::sys::LauncherPtr launcher);

  // Blocks the current thread until the modular test harness component is
  // destroyed.
  ~TestHarnessLauncher();

  // Not copyable.
  TestHarnessLauncher(const TestHarnessLauncher&) = delete;
  TestHarnessLauncher& operator=(const TestHarnessLauncher&) = delete;

  fuchsia::modular::testing::TestHarnessPtr& test_harness() {
    return test_harness_;
  }

 private:
  // In order to avoid depending on the owning thread's run loop, the test
  // harness component is launched and managed in a separate thread which
  // contains its own async loop.
  async::Loop test_harness_loop_;
  std::shared_ptr<sys::ServiceDirectory> test_harness_svc_;
  fuchsia::sys::ComponentControllerPtr test_harness_ctrl_;
  fuchsia::modular::testing::TestHarnessPtr test_harness_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TESTING_CPP_TEST_HARNESS_LAUNCHER_H_
