// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/parent_child/defs.h"

using modular::testing::Await;
using modular::testing::Fail;
using modular::testing::Get;
using modular::testing::Put;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestApp(modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    module_host->module_context()->GetLink("link", link_.NewRequest());

    link_->Get(nullptr, [this](fidl::StringPtr link_value) {
      Get("child_module_init_count", [this, link_value](fidl::StringPtr value) {
        init_count_ = atoi((*value).data());
        ++init_count_;
        Put("child_module_init_count", std::to_string(init_count_));
        FXL_LOG(INFO) << "Module initialized " << init_count_
                      << " times, link value is " << link_value << ".";
        if (link_value != std::to_string(init_count_)) {
          FXL_LOG(INFO) << "FAILURE: I was re-initialized when I shouldn't have been.";
          Fail("Child module initialized when not expected");
        }
        Signal(std::string("child_module_init_") + std::to_string(init_count_));
      });
    });
  }

  TestPoint stopped_{"Child module stopped"};

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    FXL_LOG(INFO) << "Child module exiting.";
    stopped_.Pass();

    Signal("child_module_stop");
    modular::testing::Done(done);
  }

 private:
  int init_count_;

  fuchsia::modular::LinkPtr link_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
