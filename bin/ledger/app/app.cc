// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <magenta/device/devmgr.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/load_configuration.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/network/services/network_service.fidl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {

constexpr ftl::StringView kPersistentFileSystem = "/data";
constexpr ftl::StringView kMinFsName = "minfs";
constexpr ftl::TimeDelta kMaxPollingDelay = ftl::TimeDelta::FromSeconds(10);

// App is the main entry point of the Ledger application.
//
// It is responsible for setting up the LedgerRepositoryFactory, which connects
// clients to individual Ledger instances. It should not however hold long-lived
// objects shared between Ledger instances, as we need to be able to put them in
// separate processes when the app becomes multi-instance.
class App {
 public:
  App()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
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

    if (!config.sync_params.cloud_prefix.empty()) {
      FTL_LOG(WARNING) << "The cloud_prefix configuration param is deprecated. "
                       << "Run `configure_ledger` to remove it.";
    }

    if (!configuration::SaveAsLastConfiguration(config)) {
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
    environment_ = std::make_unique<Environment>(std::move(main_runner),
                                                 network_service_.get());

    factory_impl_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        std::move(config), environment_.get());

    application_context_->outgoing_services()
        ->AddService<LedgerRepositoryFactory>(
            [this](fidl::InterfaceRequest<LedgerRepositoryFactory> request) {
              factory_bindings_.AddBinding(factory_impl_.get(),
                                           std::move(request));
            });
    return true;
  }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  std::unique_ptr<NetworkService> network_service_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  fidl::BindingSet<LedgerRepositoryFactory> factory_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

void WaitForData() {
  backoff::ExponentialBackoff backoff(ftl::TimeDelta::FromMilliseconds(10), 2,
                                      ftl::TimeDelta::FromSeconds(1));
  ftl::TimePoint now = ftl::TimePoint::Now();
  while (ftl::TimePoint::Now() - now < kMaxPollingDelay) {
    ftl::UniqueFD fd(open(kPersistentFileSystem.data(), O_RDWR));
    FTL_DCHECK(fd.is_valid());
    char out[128];
    ssize_t len = ioctl_devmgr_query_fs(fd.get(), out, sizeof(out));
    FTL_DCHECK(len >= 0);

    ftl::StringView fs_name(out, len);
    if (fs_name == kMinFsName) {
      return;
    }

    usleep(backoff.GetNext().ToMicroseconds());
  }

  FTL_LOG(WARNING) << kPersistentFileSystem
                   << " is not persistent. Did you forget to configure it?";
}

}  // namespace ledger

int main(int argc, const char** argv) {
  // Poll until /data is persistent. This is need to retrieve the Ledger
  // configuration.
  ledger::WaitForData();

  mtl::MessageLoop loop;

  ledger::App app;
  if (!app.Start(loop.task_runner())) {
    return 1;
  }

  loop.Run();
  return 0;
}
