// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/test_driver.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"

using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// The name of the test driver's child module.
constexpr char kSubModuleName[] = "test_driver_sub_module";

// Cf. README.md for what this test does and how.
class TestModule {
 public:
  TestPoint initialized_{"test driver module initialized"};

  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();
    sub_module_url_path_.push_back(modular::testing::kModuleUnderTestPath);
    test_driver_url_path_.push_back(modular::testing::kTestDriverPath);
    SetUp();
  }

  // Called via ModuleDriver.
  TestPoint stopped_{"test driver module stopped"};
  void Terminate(fit::function<void()> done) {
    stopped_.Pass();
    modular::testing::Done(std::move(done));
  }

 private:
  void SetUp() {
    module_host_->module_context()->GetLink(
        modular::testing::kTestDriverLinkName, link_.NewRequest());
    link_->Get(fidl::VectorPtr(sub_module_url_path_),
               [this](std::unique_ptr<fuchsia::mem::Buffer> link_data) {
                 std::string sub_module_url;
                 FXL_CHECK(fsl::StringFromVmo(*link_data, &sub_module_url));
                 if (!RunSubModule(sub_module_url)) {
                   Signal(modular::testing::kTestShutdown);
                   return;
                 };
               });
  }

  TestPoint test_sub_module_launched_{"sub module launched"};

  // Launches the module which is under test by the test driver.
  bool RunSubModule(const std::string& sub_module_url) {
    if (sub_module_url.empty()) {
      modular::testing::Fail("No sub_module_url supplied.");
      return false;
    }
    rapidjson::Document document;
    document.Parse(sub_module_url.c_str());
    fuchsia::modular::Intent intent;
    intent.handler = document.GetString();
    module_host_->module_context()->AddModuleToStory(
        kSubModuleName, std::move(intent), sub_module_.NewRequest(), nullptr,
        [this](const fuchsia::modular::StartModuleStatus status) {
          if (status == fuchsia::modular::StartModuleStatus::SUCCESS) {
            test_sub_module_launched_.Pass();
            RunTestDriver();
          }
        });
    return true;
  }

  void CreateTestDriverComponent(const std::string& url) {
    module_host_->startup_context()->environment()->GetLauncher(
        test_driver_launcher_.NewRequest());
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.directory_request = test_driver_services_.NewRequest();
    test_driver_launcher_->CreateComponent(
        std::move(launch_info), test_driver_component_controller_.NewRequest());
  }

  TestPoint test_driver_completed_{"test driver completed execution"};

  // Checks the return value of the test driver component after it runs to
  // completion, setting the status of the test based on the exit code: non-zero
  // is a failure, whereas zero is a success.
  void RunTestDriver() {
    link_->Get(
        fidl::VectorPtr(test_driver_url_path_),
        [this](std::unique_ptr<fuchsia::mem::Buffer> link_data) {
          if (link_data == nullptr) {
            Signal(modular::testing::kTestShutdown);
            return;
          }
          std::string json;
          FXL_CHECK(fsl::StringFromVmo(*link_data, &json));
          rapidjson::Document document;
          document.Parse(json.c_str());
          std::string test_driver_url = document.GetString();
          FXL_LOG(INFO) << "TestDriverModule launching test driver for URL: "
                        << test_driver_url;

          CreateTestDriverComponent(test_driver_url);
          test_driver_component_controller_.events().OnTerminated =
              [this](int64_t return_code,
                     fuchsia::sys::TerminationReason reason) {
                FXL_LOG(INFO)
                    << "TestDriverModule test driver returned with code : "
                    << return_code;
                if (return_code) {
                  modular::testing::Fail("Test driver failed.");
                } else {
                  test_driver_completed_.Pass();
                }
                Signal(modular::testing::kTestShutdown);
              };
        });
  }

  modular::ModuleHost* const module_host_;

  component::Services test_driver_services_;
  fuchsia::modular::LinkPtr link_;
  fuchsia::sys::LauncherPtr test_driver_launcher_;
  fuchsia::sys::ComponentControllerPtr test_driver_component_controller_;

  std::vector<std::string> sub_module_url_path_;
  std::vector<std::string> test_driver_url_path_;

  fuchsia::modular::ModuleControllerPtr sub_module_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestModule);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
