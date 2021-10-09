// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/observation/test/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <zircon/status.h>

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// This test exercises the fuchsia.ui.observation.test.Registry protocol implemented by Scenic.

namespace integration_tests {

const std::map<std::string, std::string> LocalServices() {
  return {{"fuchsia.ui.observation.geometry.Provider",
           "fuchsia-pkg://fuchsia.com/observer_integration_tests#meta/scenic.cmx"},
          {"fuchsia.ui.observation.test.Registry",
           "fuchsia-pkg://fuchsia.com/observer_integration_tests#meta/scenic.cmx"}};
}

// Allow these global services.
const std::vector<std::string> GlobalServices() {
  return {"fuchsia.vulkan.loader.Loader", "fuchsia.sysmem.Allocator"};
}

// Test fixture that sets up an environment with Registry protocol we can connect to.
class ObserverRegistryIntegrationTest : public gtest::TestWithEnvironmentFixture {
 protected:
  void SetUp() override {
    TestWithEnvironmentFixture::SetUp();

    environment_ = CreateNewEnclosingEnvironment("observer_registry_integration_test_environment",
                                                 CreateServices());
    WaitForEnclosingEnvToStart(environment_.get());
    environment_->ConnectToService(observer_registry_ptr_.NewRequest());
    observer_registry_ptr_.set_error_handler([](zx_status_t status) {
      FAIL() << "Lost connection to Observer Registry Protocol: " << zx_status_get_string(status);
    });
  }

  void TearDown() override { observer_registry_ptr_.set_error_handler(nullptr); }

  // Configures services available to the test environment. This method is called by |SetUp()|. It
  // shadows but calls |TestWithEnvironmentFixture::CreateServices()|.
  std::unique_ptr<sys::testing::EnvironmentServices> CreateServices() {
    auto services = TestWithEnvironmentFixture::CreateServices();
    for (const auto& [name, url] : LocalServices()) {
      const zx_status_t is_ok = services->AddServiceWithLaunchInfo({.url = url}, name);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << name;
    }

    for (const auto& service : GlobalServices()) {
      const zx_status_t is_ok = services->AllowParentService(service);
      FX_CHECK(is_ok == ZX_OK) << "Failed to add service " << service;
    }
    return services;
  }

  fuchsia::ui::observation::test::RegistryPtr observer_registry_ptr_;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
};

TEST_F(ObserverRegistryIntegrationTest, RegistryProtocolConnectedSuccess) {
  bool result = false;
  fuchsia::ui::observation::geometry::ProviderHandle listener_handle;
  auto listener_request = listener_handle.NewRequest();
  observer_registry_ptr_->RegisterGlobalGeometryProvider(std::move(listener_request),
                                                         [&result] { result = true; });
  RunLoopUntil([&result] { return result; });
  EXPECT_TRUE(result);
}
}  // namespace integration_tests
