// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/entity/cpp/json.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

#include "peridot/bin/module_resolver/local_module_resolver.h"
#include "peridot/lib/module_manifest_source/firebase_source.h"
#include "peridot/lib/module_manifest_source/module_package_source.h"

namespace modular {
namespace {

namespace http = ::fuchsia::net::oldhttp;

class ModuleResolverApp {
 public:
  ModuleResolverApp(component::StartupContext* const context) {
    resolver_impl_ = std::make_unique<LocalModuleResolver>();
    // Set up |resolver_impl_|.
    resolver_impl_->AddSource("module_package",
                              std::make_unique<ModulePackageSource>(context));

    context->outgoing().AddPublicService<fuchsia::modular::ModuleResolver>(
        [this](
            fidl::InterfaceRequest<fuchsia::modular::ModuleResolver> request) {
          resolver_impl_->Connect(std::move(request));
        });
  }

  void Terminate(fit::function<void()> done) { done(); }

 private:
  std::unique_ptr<LocalModuleResolver> resolver_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleResolverApp);
};

}  // namespace
}  // namespace modular

const char kUsage[] = R"USAGE(%s)USAGE";

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    printf(kUsage, argv[0]);
    return 0;
  }
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<modular::ModuleResolverApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::ModuleResolverApp>(context.get()),
      [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
