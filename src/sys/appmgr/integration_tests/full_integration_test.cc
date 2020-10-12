// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/power/statecontrol/cpp/fidl_test_base.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <zircon/errors.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/macros.h"

using fuchsia::sys::TerminationReason;
using sys::testing::EnclosingEnvironment;
using sys::testing::EnvironmentServices;
using sys::testing::TestWithEnvironment;
using PowerAdmin = fuchsia::hardware::power::statecontrol::Admin;
using fuchsia::hardware::power::statecontrol::Admin_Reboot_Result;
using fuchsia::hardware::power::statecontrol::RebootReason;
using fuchsia::hardware::power::statecontrol::testing::Admin_TestBase;

namespace component {
namespace {

class MockPowerAdmin : public Admin_TestBase {
 public:
  MockPowerAdmin() = default;
  ~MockPowerAdmin() override = default;

  fidl::InterfaceRequestHandler<PowerAdmin> GetHandler() { return bindings_.GetHandler(this); }
  bool rebooted() const { return rebooted_; }
  RebootReason reboot_reason() const { return reboot_reason_; }

  void Reboot(RebootReason reason, PowerAdmin::RebootCallback cb) override {
    rebooted_ = true;
    reboot_reason_ = reason;
    cb(Admin_Reboot_Result::WithResponse({}));
  }

  // Admin_TestBase implementation
  void NotImplemented_(const std::string& name) final {}

 private:
  fidl::BindingSet<PowerAdmin> bindings_;
  bool rebooted_ = false;
  RebootReason reboot_reason_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockPowerAdmin);
};

class AppmgrTest : public TestWithEnvironment {
 protected:
  void SetUp() override {
    auto services = CreateServices();
    ASSERT_EQ(ZX_OK, services->AddService(power_admin_.GetHandler()));
    ASSERT_EQ(ZX_OK, services->AllowParentService(fuchsia::logger::Log::Name_));
    env_ = CreateNewEnclosingEnvironment("enclosing-env", std::move(services));
  }

  fuchsia::sys::ComponentControllerPtr RunComponent(const std::string& url) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    return env_->CreateComponent(std::move(launch_info));
  }

  MockPowerAdmin power_admin_;
  std::unique_ptr<EnclosingEnvironment> env_;
};

TEST_F(AppmgrTest, RebootIfSysmgrExits) {
  auto controller =
      RunComponent("fuchsia-pkg://fuchsia.com/appmgr_integration_tests#meta/failing_appmgr.cmx");
  RunLoopUntil([this] { return power_admin_.rebooted(); });
  EXPECT_EQ(power_admin_.reboot_reason(), RebootReason::SYSTEM_FAILURE);
}

}  // namespace
}  // namespace component
