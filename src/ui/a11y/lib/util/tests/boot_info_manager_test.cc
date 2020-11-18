// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/util/boot_info_manager.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <future>

#include <gtest/gtest.h>

namespace accessibility_test {

class FakeLastRebootInfoProvider : fuchsia::feedback::testing::LastRebootInfoProvider_TestBase {
 public:
  FakeLastRebootInfoProvider() = default;
  ~FakeLastRebootInfoProvider() override = default;

  void SetLastReboot(fuchsia::feedback::LastReboot last_reboot) {
    last_reboot_ = std::move(last_reboot);
  }

  fidl::InterfaceRequestHandler<fuchsia::feedback::LastRebootInfoProvider> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this,
            dispatcher](fidl::InterfaceRequest<fuchsia::feedback::LastRebootInfoProvider> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  void Get(GetCallback callback) override { callback(std::move(last_reboot_)); }

  void NotImplemented_(const std::string& name) override {
    ADD_FAILURE() << "Unimplemented function called: " << name;
  }

 private:
  fidl::BindingSet<fuchsia::feedback::LastRebootInfoProvider> bindings_;
  fuchsia::feedback::LastReboot last_reboot_;
};

class BootInfoManagerTest : public gtest::TestLoopFixture {
 public:
  BootInfoManagerTest() = default;
  ~BootInfoManagerTest() override = default;

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    fake_last_reboot_info_provider_ = std::make_unique<FakeLastRebootInfoProvider>();

    context_provider_.service_directory_provider()->AddService(
        fake_last_reboot_info_provider_->GetHandler(dispatcher()));
    RunLoopUntilIdle();

    boot_info_manager_ = std::make_unique<a11y::BootInfoManager>(context_provider_.context());
    RunLoopUntilIdle();
  }

  bool CallLastRebootWasUserInitiatedAndGetResult() {
    // The BootInfoManager uses a synchronous fidl connection. In a prod
    // environment, the client and server would be running in separate
    // processes. However, in this test, unless we explicitly start a separate
    // thread for the client call, the server's response will be blocked.
    std::future<bool> result =
        std::async([this]() { return boot_info_manager_->LastRebootWasUserInitiated(); });

    while (result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
      // Run the main thread's loop, allowing the server object to respond to requests.
      RunLoopUntilIdle();
    }
    return result.get();
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<FakeLastRebootInfoProvider> fake_last_reboot_info_provider_;
  std::unique_ptr<a11y::BootInfoManager> boot_info_manager_;
};

TEST_F(BootInfoManagerTest, BootInfoManagerUserInitiated) {
  fuchsia::feedback::LastReboot last_reboot;
  last_reboot.set_reason(fuchsia::feedback::RebootReason::USER_REQUEST);
  fake_last_reboot_info_provider_->SetLastReboot(std::move(last_reboot));
  EXPECT_TRUE(CallLastRebootWasUserInitiatedAndGetResult());
}

TEST_F(BootInfoManagerTest, BootInfoManagerSystemInitiated) {
  fuchsia::feedback::LastReboot last_reboot;
  last_reboot.set_reason(fuchsia::feedback::RebootReason::SYSTEM_UPDATE);
  fake_last_reboot_info_provider_->SetLastReboot(std::move(last_reboot));
  EXPECT_FALSE(CallLastRebootWasUserInitiatedAndGetResult());
}

}  // namespace accessibility_test
