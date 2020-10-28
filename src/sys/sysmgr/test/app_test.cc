// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/sysmgr/app.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/string.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_interceptor.h>

#include <gtest/gtest.h>

#include "lib/vfs/cpp/composed_service_dir.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/strings/substitute.h"

namespace sysmgr {
namespace {

class TestSysmgr : public ::testing::Test, fuchsia::hardware::power::statecontrol::Admin {
 protected:
  TestSysmgr() : admin_binding_(this), on_reboot_([](auto) {}) {}
  // Makes a Config object with contents filled with |json| and returns its paths.
  // This method should only be called once in a test case.
  ::sysmgr::Config NewConfig(const std::string& json) {
    std::string tmp_dir_path;
    tmp_dir_.NewTempDir(&tmp_dir_path);
    const std::string json_file = fxl::Substitute("$0/sysmgr.config", tmp_dir_path);
    ZX_ASSERT(files::WriteFile(json_file, json.data(), json.size()));
    Config config;
    config.ParseFromDirectory(tmp_dir_path);
    ZX_ASSERT(!config.HasError());
    return config;
  }

  // |fuchsia::hardware::power::statecontrol::Admin|
  void PowerFullyOn(PowerFullyOnCallback callback) override {}
  // |fuchsia::hardware::power::statecontrol::Admin|
  void Reboot(fuchsia::hardware::power::statecontrol::RebootReason reason,
              RebootCallback callback) override {
    on_reboot_(reason);
    callback(fit::ok());
  }
  // |fuchsia::hardware::power::statecontrol::Admin|
  void RebootToBootloader(RebootToBootloaderCallback callback) override {}
  // |fuchsia::hardware::power::statecontrol::Admin|
  void RebootToRecovery(RebootToRecoveryCallback callback) override {}
  // |fuchsia::hardware::power::statecontrol::Admin|
  void Poweroff(PoweroffCallback callback) override {}
  // |fuchsia::hardware::power::statecontrol::Admin|
  void Mexec(MexecCallback callback) override {}
  // |fuchsia::hardware::power::statecontrol::Admin|
  void SuspendToRam(SuspendToRamCallback callback) override {}

  fidl::Binding<fuchsia::hardware::power::statecontrol::Admin> admin_binding_;
  fit::function<void(fuchsia::hardware::power::statecontrol::RebootReason)> on_reboot_;

  files::ScopedTempDir tmp_dir_;
};

// Smoke test that LaunchComponent doesn't crash.
TEST_F(TestSysmgr, LaunchComponent) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ::sysmgr::Config config = NewConfig(R"({})");
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  ::sysmgr::App app(std::move(config), component_context->svc(), &loop);

  auto launch_info =
      fuchsia::sys::LaunchInfo{.url = "fuchsia-pkg://example.com/pkg#meta/component.cmx"};
  app.LaunchComponent(std::move(launch_info), nullptr, nullptr);
}

// Test that a critical component crashing results in the system rebooting.
// This is accomplished by configuring a component which doesn't exist as a critical component.
TEST_F(TestSysmgr, CrashingCriticalComponent) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ::sysmgr::Config config = NewConfig(R"({
    "services": {
      "example.random.service": "fuchsia-pkg://example.com/pkg#meta/component.cmx"
    },
    "startup_services": ["example.random.service"],
    "critical_components": ["fuchsia-pkg://example.com/pkg#meta/component.cmx"]
  })");
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  vfs::ComposedServiceDir composed_svc_dir;
  fidl::InterfaceHandle<fuchsia::io::Directory> fallback_svc_dir;
  component_context->svc()->CloneChannel(fallback_svc_dir.NewRequest());
  composed_svc_dir.set_fallback(std::move(fallback_svc_dir));
  composed_svc_dir.AddService(
      fuchsia::hardware::power::statecontrol::Admin::Name_,
      std::make_unique<vfs::Service>(
          [this](zx::channel request, async_dispatcher_t* dispatcher) mutable {
            admin_binding_.Bind(std::move(request));
          }));

  fidl::InterfaceRequest<fuchsia::io::Directory> dir_request;
  auto svc_dir = sys::ServiceDirectory::CreateWithRequest(&dir_request);
  composed_svc_dir.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE |
                             fuchsia::io::OPEN_FLAG_DIRECTORY,
                         dir_request.TakeChannel(), loop.dispatcher());

  ::sysmgr::App app(std::move(config), svc_dir, &loop);
  on_reboot_ = [&](auto reason) {
    EXPECT_EQ(reason,
              fuchsia::hardware::power::statecontrol::RebootReason::CRITICAL_COMPONENT_FAILURE);
    loop.Quit();
  };
  loop.Run();
}

}  // namespace
}  // namespace sysmgr
