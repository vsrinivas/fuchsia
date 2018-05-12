// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <views_v1/cpp/fidl.h>
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"

using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// The NullModule just sits there and does nothing until it's terminated.
class NullModule {
 public:
  TestPoint initialized_{"Null module initialized"};

  NullModule(
      modular::ModuleHost* const module_host,
      fidl::InterfaceRequest<views_v1::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<component::ServiceProvider> /*outgoing_services*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host_->application_context(), __FILE__);
    initialized_.Pass();
    Signal(kCommonNullModuleStarted);
  }

  TestPoint stopped_{"Null module stopped"};

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    Signal(kCommonNullModuleStopped);
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  modular::ModuleHost* const module_host_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<NullModule> driver(app_context.get(),
                                           [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
