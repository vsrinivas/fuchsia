// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/errors.h>

#include <memory>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {

// Augments the |TestLoopFixture| with a |ServiceDirectoryProvider| to easily inject service
// providers in unit tests.
class UnitTestFixture : public gtest::TestLoopFixture {
 public:
  UnitTestFixture() : service_directory_provider_(dispatcher()) {}

 protected:
  std::shared_ptr<sys::ServiceDirectory>& services() {
    return service_directory_provider_.service_directory();
  }

  template <typename ServiceProvider>
  void InjectServiceProvider(ServiceProvider* service_provider) {
    ASSERT_EQ(service_directory_provider_.AddService(service_provider->GetHandler()), ZX_OK);
  }

 private:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
};

}  // namespace feedback
