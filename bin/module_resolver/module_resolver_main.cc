// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/module_resolver/fidl/module_resolver.fidl.h"

#include "peridot/bin/module_resolver/module_resolver_impl.h"
#include "peridot/lib/module_manifest_repository/directory_repository/directory_repository.h"

namespace maxwell {
namespace {

// NOTE: This must match the path specified in
// build/module_repository/publish.gni
constexpr char kModuleRepositoryPath[] =
    "/system/data/module_manifest_repository";

class ModuleResolverApp {
 public:
  ModuleResolverApp(app::ApplicationContext* const context) {
    resolver_impl_.AddRepository(
        "local_ro",
        std::make_unique<modular::DirectoryRepository>(kModuleRepositoryPath));
    context->outgoing_services()->AddService<modular::ModuleResolver>(
        [this](fidl::InterfaceRequest<modular::ModuleResolver> request) {
          resolver_impl_.Connect(std::move(request));
        });
  }

  void Terminate(const std::function<void()>& done) { done(); }

 private:
  ModuleResolverImpl resolver_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleResolverApp);
};

}  // namespace
}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<maxwell::ModuleResolverApp> driver(
      context->outgoing_services(),
      std::make_unique<maxwell::ModuleResolverApp>(context.get()),
      [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
