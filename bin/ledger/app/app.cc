// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include <magenta/device/vfs.h>
#include <trace-provider/provider.h>

#include "lib/app/cpp/application_context.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/src/app/erase_remote_repository_operation.h"
#include "apps/ledger/src/app/ledger_repository_factory_impl.h"
#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/cobalt/cobalt.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/ledger/src/network/no_network_service.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {

namespace {

constexpr fxl::StringView kPersistentFileSystem = "/data";
constexpr fxl::StringView kMinFsName = "minfs";
constexpr fxl::TimeDelta kMaxPollingDelay = fxl::TimeDelta::FromSeconds(10);
constexpr fxl::StringView kNoMinFsFlag = "no_minfs_wait";
constexpr fxl::StringView kNoPersistedConfig = "no_persisted_config";
constexpr fxl::StringView kNoNetworkForTesting = "no_network_for_testing";
constexpr fxl::StringView kNoStatisticsReporting =
    "no_statistics_reporting_for_testing";
constexpr fxl::StringView kTriggerCloudErasedForTesting =
    "trigger_cloud_erased_for_testing";

struct AppParams {
  LedgerRepositoryFactoryImpl::ConfigPersistence config_persistence =
      LedgerRepositoryFactoryImpl::ConfigPersistence::PERSIST;
  bool no_network_for_testing = false;
  bool trigger_cloud_erased_for_testing = false;
  bool disable_statistics = false;
};

fxl::AutoCall<fxl::Closure> SetupCobalt(
    bool disable_statistics,
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* application_context) {
  if (disable_statistics) {
    return fxl::MakeAutoCall<fxl::Closure>([] {});
  }
  return InitializeCobalt(std::move(task_runner), application_context);
};

// App is the main entry point of the Ledger application.
//
// It is responsible for setting up the LedgerRepositoryFactory, which connects
// clients to individual Ledger instances. It should not however hold long-lived
// objects shared between Ledger instances, as we need to be able to put them in
// separate processes when the app becomes multi-instance.
class App : public LedgerController,
            public LedgerRepositoryFactoryImpl::Delegate {
 public:
  explicit App(AppParams app_params)
      : app_params_(app_params),
        trace_provider_(loop_.async()),
        application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        cobalt_cleaner_(SetupCobalt(app_params_.disable_statistics,
                                    loop_.task_runner(),
                                    application_context_.get())),
        config_persistence_(app_params_.config_persistence) {
    FXL_DCHECK(application_context_);

    ReportEvent(CobaltEvent::LEDGER_STARTED);
  }
  ~App() override {}

  bool Start() {
    if (app_params_.no_network_for_testing) {
      network_service_ =
          std::make_unique<ledger::NoNetworkService>(loop_.task_runner());
    } else {
      network_service_ = std::make_unique<ledger::NetworkServiceImpl>(
          loop_.task_runner(), [this] {
            return application_context_
                ->ConnectToEnvironmentService<network::NetworkService>();
          });
    }
    environment_ = std::make_unique<Environment>(loop_.task_runner(),
                                                 network_service_.get());
    if (app_params_.trigger_cloud_erased_for_testing) {
      environment_->SetTriggerCloudErasedForTesting();
    }

    factory_impl_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        this, environment_.get(), config_persistence_);

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
  void Terminate() override {
    // Wait for pending asynchronous operations on the
    // LedgerRepositoryFactoryImpl, such as erasing a repository, but do not
    // allow new requests to be started in the meantime.
    shutdown_in_progress_ = true;
    factory_bindings_.CloseAllBindings();
    application_context_->outgoing_services()->Close();
    factory_impl_.reset();

    if (pending_operation_manager_.size() == 0u) {
      // If we still have pending operations, we will post the quit task when
      // the last one completes.
      loop_.PostQuitTask();
    }
  }

  // LedgerRepositoryFactoryImpl::Delegate:
  void EraseRepository(
      EraseRemoteRepositoryOperation erase_remote_repository_operation,
      std::function<void(bool)> callback) override {
    auto handler = pending_operation_manager_.Manage(
        std::move(erase_remote_repository_operation));
    handler.first->Start([
      this, cleanup = std::move(handler.second), callback = std::move(callback)
    ](bool succeeded) {
      callback(succeeded);
      // This lambda is deleted in |cleanup()|, don't access captured members
      // afterwards.
      cleanup();
      CheckPendingOperations();
    });
  }

  void CheckPendingOperations() {
    if (shutdown_in_progress_ && pending_operation_manager_.size() == 0u) {
      loop_.PostQuitTask();
    }
  }

  bool shutdown_in_progress_ = false;
  const AppParams app_params_;
  mtl::MessageLoop loop_;
  trace::TraceProvider trace_provider_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  fxl::AutoCall<fxl::Closure> cobalt_cleaner_;
  const LedgerRepositoryFactoryImpl::ConfigPersistence config_persistence_;
  std::unique_ptr<NetworkService> network_service_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  fidl::BindingSet<LedgerRepositoryFactory> factory_bindings_;
  fidl::BindingSet<LedgerController> controller_bindings_;
  callback::PendingOperationManager pending_operation_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

void WaitForData() {
  backoff::ExponentialBackoff backoff(fxl::TimeDelta::FromMilliseconds(10), 2,
                                      fxl::TimeDelta::FromSeconds(1));
  fxl::TimePoint now = fxl::TimePoint::Now();
  while (fxl::TimePoint::Now() - now < kMaxPollingDelay) {
    fxl::UniqueFD fd(open(kPersistentFileSystem.data(), O_RDWR));
    FXL_DCHECK(fd.is_valid());
    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t len = ioctl_vfs_query_fs(fd.get(), info, sizeof(buf) - 1);
    FXL_DCHECK(len > static_cast<ssize_t>(sizeof(vfs_query_info_t)));
    fxl::StringView fs_name(info->name, len - sizeof(vfs_query_info_t));

    if (fs_name == kMinFsName) {
      return;
    }

    usleep(backoff.GetNext().ToMicroseconds());
  }

  FXL_LOG(WARNING) << kPersistentFileSystem
                   << " is not persistent. Did you forget to configure it?";
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  ledger::AppParams app_params;
  if (command_line.HasOption(ledger::kNoPersistedConfig)) {
    app_params.config_persistence =
        ledger::LedgerRepositoryFactoryImpl::ConfigPersistence::FORGET;
  }
  app_params.no_network_for_testing =
      command_line.HasOption(ledger::kNoNetworkForTesting);
  app_params.trigger_cloud_erased_for_testing =
      command_line.HasOption(ledger::kTriggerCloudErasedForTesting);
  app_params.disable_statistics =
      command_line.HasOption(ledger::kNoStatisticsReporting);

  if (!command_line.HasOption(ledger::kNoMinFsFlag.ToString())) {
    // Poll until /data is persistent. This is need to retrieve the Ledger
    // configuration.
    ledger::WaitForData();
  }

  ledger::App app(app_params);
  if (!app.Start()) {
    return 1;
  }

  return 0;
}
