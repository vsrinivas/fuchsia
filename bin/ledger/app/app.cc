// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <errno.h>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/load_configuration.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/network/services/network_service.fidl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {

// App is the main entry point of the Ledger application.
//
// It is responsible for setting up the LedgerRepositoryFactory, which connects
// clients to individual Ledger instances. It should not however hold long-lived
// objects shared between Ledger instances, as we need to be able to put them in
// separate processes when the app becomes multi-instance.
class App {
 public:
  App()
      : application_context_(
            modular::ApplicationContext::CreateFromStartupInfo()) {
    FTL_DCHECK(application_context_);
    tracing::InitializeTracer(application_context_.get(), {"ledger"});
  }
  ~App() {}

  bool Start(ftl::RefPtr<ftl::TaskRunner> main_runner) {
    configuration::Configuration config;
    if (!LoadConfiguration(&config)) {
      FTL_LOG(ERROR) << "Ledger is misconfigured, quitting.";
      return false;
    }

    if (!SaveAsLastConfiguration(config)) {
      FTL_LOG(ERROR) << "Failed to save the current configuration for "
                     << "compatibility check, quitting.";
      return false;
    }

    if (config.use_sync) {
      network_service_ =
          std::make_unique<ledger::NetworkServiceImpl>(main_runner, [this] {
            return application_context_
                ->ConnectToEnvironmentService<network::NetworkService>();
          });
    }
    environment_ = std::make_unique<Environment>(
        std::move(config), std::move(main_runner), network_service_.get());

    factory_impl_ =
        std::make_unique<LedgerRepositoryFactoryImpl>(environment_.get());

    application_context_->outgoing_services()
        ->AddService<LedgerRepositoryFactory>([this](
            fidl::InterfaceRequest<LedgerRepositoryFactory> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
    return true;
  }

 private:
  std::unique_ptr<modular::ApplicationContext> application_context_;
  std::unique_ptr<NetworkService> network_service_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  fidl::BindingSet<LedgerRepositoryFactory> factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace ledger

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  ledger::App app;
  if (!app.Start(loop.task_runner())) {
    return 1;
  }

  loop.Run();
  return 0;
}
