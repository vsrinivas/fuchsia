// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/termina_guest_manager/termina_guest_manager.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/virtualization/bin/termina_guest_manager/termina_config.h"

namespace termina_guest_manager {
namespace {
using ConfigResult = fit::result<::fuchsia::virtualization::GuestManagerError,
                                 ::fuchsia::virtualization::GuestConfig>;

class TestTerminaGuestManager : public TerminaGuestManager {
 public:
  TestTerminaGuestManager(async_dispatcher_t* dispatcher,
                          std::unique_ptr<sys::ComponentContext> context,
                          termina_config::Config structured_config,
                          fit::function<void()> stop_manager_callback, ConfigResult config_result)
      : TerminaGuestManager(dispatcher, std::move(context), std::move(structured_config),
                            std::move(stop_manager_callback)),
        config_result_(std::move(config_result)) {}

  ConfigResult GetDefaultGuestConfig() { return std::move(config_result_); }

 private:
  ConfigResult config_result_;
};

class TerminaGuestManagerTest : public gtest::TestLoopFixture {};

TEST_F(TerminaGuestManagerTest, LaunchFail) {
  termina_config::Config structured_config;
  sys::testing::ComponentContextProvider provider;

  ConfigResult config = fit::error(::fuchsia::virtualization::GuestManagerError::BAD_CONFIG);
  TestTerminaGuestManager manager(
      dispatcher(), provider.TakeContext(), std::move(structured_config), [] {}, std::move(config));

  fuchsia::virtualization::LinuxManagerPtr linux_manager;
  provider.ConnectToPublicService(linux_manager.NewRequest());

  std::optional<fuchsia::virtualization::LinuxManager_StartAndGetLinuxGuestInfo_Result> guest_info;
  linux_manager->StartAndGetLinuxGuestInfo(
      "termina", [&guest_info](auto info) mutable { guest_info = std::move(info); });
  RunLoopUntilIdle();

  ASSERT_TRUE(guest_info.has_value());
  ASSERT_TRUE(guest_info->is_response());
  EXPECT_EQ(guest_info->response().info.container_status(),
            fuchsia::virtualization::ContainerStatus::FAILED);
}

}  // namespace
}  // namespace termina_guest_manager
