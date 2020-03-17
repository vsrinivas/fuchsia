// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_UNIT_TEST_FIXTURE_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_UNIT_TEST_FIXTURE_H_

#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/errors.h>

#include <memory>

#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {

// Augments the |TestLoopFixture| with a |ServiceDirectoryProvider| to easily inject service
// providers in unit tests.
class UnitTestFixture : public gtest::TestLoopFixture {
 public:
  UnitTestFixture() : service_directory_provider_(dispatcher()) {}

  template <typename ServiceProvider>
  void InjectServiceProvider(ServiceProvider* service_provider) {
    FX_CHECK(service_directory_provider_.AddService(service_provider->GetHandler()) == ZX_OK);
  }

 protected:
  std::shared_ptr<sys::ServiceDirectory>& services() {
    return service_directory_provider_.service_directory();
  }

 private:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_UNIT_TEST_FIXTURE_H_
