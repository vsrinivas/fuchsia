// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <magenta/device/vfs.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/network/services/network_service.fidl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {

constexpr ftl::StringView kPersistentFileSystem = "/data";
constexpr ftl::StringView kMinFsName = "minfs";
constexpr ftl::TimeDelta kMaxPollingDelay = ftl::TimeDelta::FromSeconds(10);
constexpr ftl::StringView kNoMinFsFlag = "no_minfs_wait";
constexpr ftl::StringView kNoPersistedConfig = "no_persisted_config";

// App is the main entry point of the Ledger application.
//
// It is responsible for setting up the LedgerRepositoryFactory, which connects
// clients to individual Ledger instances. It should not however hold long-lived
// objects shared between Ledger instances, as we need to be able to put them in
// separate processes when the app becomes multi-instance.
class App : public LedgerController {
 public:
  App(LedgerRepositoryFactoryImpl::ConfigPersistence config_persistence)
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        config_persistence_(config_persistence) {
    FTL_DCHECK(application_context_);
    tracing::InitializeTracer(application_context_.get(), {"ledger"});
  }
  ~App() {}

  bool Start() {
    network_service_ = std::make_unique<ledger::NetworkServiceImpl>(
        loop_.task_runner(), [this] {
          return application_context_
              ->ConnectToEnvironmentService<network::NetworkService>();
        });
    environment_ = std::make_unique<Environment>(loop_.task_runner(),
                                                 network_service_.get());

    factory_impl_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        environment_.get(), config_persistence_);

    application_context_->outgoing_services()
        ->AddService<LedgerRepositoryFactory>(
            [this](fidl::InterfaceRequest<LedgerRepositoryFactory> request) {
              factory_bindings_.AddBinding(factory_impl_.get(),
                                           std::move(request));
            });
    application_context_->outgoing_services()->AddService<LedgerController>(
        [this](fidl::InterfaceRequest<LedgerController> request) {
          controller_bindings_.AddBinding(this, std::move(request));
        });

    loop_.Run();

    return true;
  }

 private:
  // LedgerController implementation.
  void Terminate() override { loop_.PostQuitTask(); }

  mtl::MessageLoop loop_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  const LedgerRepositoryFactoryImpl::ConfigPersistence config_persistence_;
  std::unique_ptr<NetworkService> network_service_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  fidl::BindingSet<LedgerRepositoryFactory> factory_bindings_;
  fidl::BindingSet<LedgerController> controller_bindings_;

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
    ssize_t len = ioctl_vfs_query_fs(fd.get(), out, sizeof(out));
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
  const auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  ftl::SetLogSettingsFromCommandLine(command_line);

  if (!command_line.HasOption(ledger::kNoMinFsFlag.ToString())) {
    // Poll until /data is persistent. This is need to retrieve the Ledger
    // configuration.
    ledger::WaitForData();
  }

  ledger::LedgerRepositoryFactoryImpl::ConfigPersistence config_persistence =
      command_line.HasOption(ledger::kNoPersistedConfig.ToString())
          ? ledger::LedgerRepositoryFactoryImpl::ConfigPersistence::FORGET
          : ledger::LedgerRepositoryFactoryImpl::ConfigPersistence::PERSIST;
  ledger::App app(config_persistence);
  if (!app.Start()) {
    return 1;
  }

  return 0;
}
