// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/testing/cpp/test_harness_launcher.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

namespace modular_testing {

// TestHarnessFixture is a googletest fixture that starts up the Modular test harness component
// and provides the |fuchsia.modular.testing.TestHarness| service.
class TestHarnessFixture : public sys::testing::TestWithEnvironment {
 public:
  TestHarnessFixture();

 protected:
  void TearDown() override;

  // Returns the |TestHarnessLauncher| started by this fixture.
  modular_testing::TestHarnessLauncher* test_harness_launcher() { return &test_harness_launcher_; }

  const fuchsia::modular::testing::TestHarnessPtr& test_harness() {
    return test_harness_launcher_.test_harness();
  }

 private:
  modular_testing::TestHarnessLauncher test_harness_launcher_;
};

// Starts a new mod by the given |intent| and |mod_name| in a new story given
// by |story_name|.
void AddModToStory(const fuchsia::modular::testing::TestHarnessPtr& test_harness,
                   std::string story_name, std::string mod_name, fuchsia::modular::Intent intent);

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_TEST_HARNESS_FIXTURE_H_
