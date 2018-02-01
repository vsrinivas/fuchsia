// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/story/fidl/chain.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

class TestApp {
 public:
  TestApp(
      modular::ModuleHost* module_host,
      fidl::InterfaceRequest<mozart::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      : module_context_(module_host->module_context()) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    initialized_.Pass();
    module_context_->GetChain(chain_.NewRequest());
    GetKeys();
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  void GetKeys() {
    chain_->GetKeys([this](const fidl::Array<fidl::String>& keys) {
      if (keys.empty()) {
        getkeys_.Pass();
      }

      module_context_->Done();
    });
  }

  modular::ModuleContext* module_context_;

  TestPoint initialized_{"Child module initialized"};
  TestPoint getkeys_{"Got correct Chain.GetKeys() response"};
  TestPoint stopped_{"Child module stopped"};
  modular::ChainPtr chain_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(app_context.get(),
                                        [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
