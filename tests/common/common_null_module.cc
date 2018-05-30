// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"

using fuchsia::modular::testing::Signal;
using fuchsia::modular::testing::TestPoint;

namespace {

// The NullModule just sits there and does nothing until it's terminated.
class NullModule {
 public:
  TestPoint initialized_{"Null module initialized"};

  NullModule(fuchsia::modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    fuchsia::modular::testing::Init(module_host_->startup_context(), __FILE__);
    initialized_.Pass();
    Signal(kCommonNullModuleStarted);
  }

  TestPoint stopped_{"Null module stopped"};

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    Signal(kCommonNullModuleStopped);
    stopped_.Pass();
    fuchsia::modular::testing::Done(done);
  }

 private:
  fuchsia::modular::ModuleHost* const module_host_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  fuchsia::modular::ModuleDriver<NullModule> driver(
      context.get(), [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
