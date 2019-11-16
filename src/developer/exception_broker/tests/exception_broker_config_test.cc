// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>

#include <gtest/gtest.h>

#include "src/developer/exception_broker/exception_broker.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace fuchsia {
namespace exception {

namespace {

constexpr char kTestConfigFile[] = "/pkg/data/enable_jitd_on_startup.json";

}  // namespace


TEST(ConfigText, ShouldBeActive) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  sys::testing::ServiceDirectoryProvider services;

  auto broker = fuchsia::exception::ExceptionBroker::Create(
      loop.dispatcher(), services.service_directory(), kTestConfigFile);

  ASSERT_TRUE(broker->limbo_manager().active());
}

}  // namespace exception
}  // namespace fuchsia

int main(int argc, char* argv[]) {
  if (!fxl::SetTestSettings(argc, argv))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"exception-broker", "integration-test"});

  return RUN_ALL_TESTS();
}
