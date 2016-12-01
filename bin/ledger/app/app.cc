// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <errno.h>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/files/file.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {

// App is the main entry point of the Ledger Mojo application.
//
// It is responsible for setting up the LedgerRepositoryFactory, which connects
// clients to
// individual ledger instances. It should not however hold long-lived objects
// shared between ledger instances, as we need to be able to put them in
// separate processes when the app becomes multi-instance.
class App {
 public:
  App(ftl::RefPtr<ftl::TaskRunner> main_runner)
      : application_context_(
            modular::ApplicationContext::CreateFromStartupInfo()) {
    FTL_DCHECK(application_context_);

    std::string configuration_file =
        configuration::kDefaultConfigurationFile.ToString();
    configuration::Configuration configuration;
    if (files::IsFile(configuration_file)) {
      if (configuration::ConfigurationEncoder::Decode(configuration_file,
                                                      &configuration)) {
        FTL_LOG(INFO) << "Read the configuration file at "
                      << configuration::kDefaultConfigurationFile;
      }
    } else {
      FTL_LOG(WARNING)
          << "No configuration file for Ledger. Using default configuration";
    }

    if (configuration.use_sync) {
      network_service_ = std::make_unique<ledger::NetworkServiceImpl>([this] {
        return application_context_
            ->ConnectToEnvironmentService<network::NetworkService>();
      });
    }
    environment_ = std::make_unique<Environment>(std::move(configuration),
                                                 std::move(main_runner),
                                                 network_service_.get());

    factory_impl_ =
        std::make_unique<LedgerRepositoryFactoryImpl>(environment_.get());

    application_context_->outgoing_services()
        ->AddService<LedgerRepositoryFactory>([this](
            fidl::InterfaceRequest<LedgerRepositoryFactory> request) {
          factory_bindings_.AddBinding(factory_impl_.get(), std::move(request));
        });
  }
  ~App() {}

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

  ledger::App app(loop.task_runner());

  loop.Run();
  return 0;
}
