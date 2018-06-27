// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <utility>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>
#include <zircon/device/vfs.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/backoff/exponential_backoff.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/cobalt/cobalt.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_factory_impl.h"

namespace ledger {

namespace {

constexpr fxl::StringView kPersistentFileSystem = "/data";
constexpr fxl::StringView kMinFsName = "minfs";
constexpr zx::duration kMaxPollingDelay = zx::sec(10);
constexpr fxl::StringView kNoMinFsFlag = "no_minfs_wait";
constexpr fxl::StringView kNoStatisticsReporting = "disable_reporting";

struct AppParams {
  bool disable_statistics = false;
};

fxl::AutoCall<fit::closure> SetupCobalt(
    bool disable_statistics, async_t* async,
    fuchsia::sys::StartupContext* startup_context) {
  if (disable_statistics) {
    return fxl::MakeAutoCall<fit::closure>([] {});
  }
  return InitializeCobalt(async, startup_context);
};

// App is the main entry point of the Ledger application.
//
// It is responsible for setting up the LedgerRepositoryFactory, which connects
// clients to individual Ledger instances. It should not however hold long-lived
// objects shared between Ledger instances, as we need to be able to put them in
// separate processes when the app becomes multi-instance.
class App : public ledger_internal::LedgerController {
 public:
  explicit App(AppParams app_params)
      : app_params_(app_params),
        loop_(&kAsyncLoopConfigMakeDefault),
        trace_provider_(loop_.async()),
        startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
        cobalt_cleaner_(SetupCobalt(app_params_.disable_statistics,
                                    loop_.async(), startup_context_.get())) {
    FXL_DCHECK(startup_context_);

    ReportEvent(CobaltEvent::LEDGER_STARTED);
  }
  ~App() override {}

  bool Start() {
    environment_ = std::make_unique<Environment>(
        EnvironmentBuilder().SetAsync(loop_.async()).Build());
    auto user_communicator_factory =
        std::make_unique<p2p_sync::UserCommunicatorFactoryImpl>(
            environment_.get(), startup_context_.get(),
            app_params_.disable_statistics ? "" : "ledger_p2p");

    factory_impl_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        environment_.get(), std::move(user_communicator_factory));

    startup_context_->outgoing()
        .AddPublicService<ledger_internal::LedgerRepositoryFactory>(
            [this](
                fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
                    request) {
              factory_bindings_.AddBinding(factory_impl_.get(),
                                           std::move(request));
            });
    startup_context_->outgoing().AddPublicService<LedgerController>(
        [this](fidl::InterfaceRequest<LedgerController> request) {
          controller_bindings_.AddBinding(this, std::move(request));
        });

    loop_.Run();

    return true;
  }

 private:
  // LedgerController:
  void Terminate() override { loop_.Quit(); }

  const AppParams app_params_;
  async::Loop loop_;
  trace::TraceProvider trace_provider_;
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  fxl::AutoCall<fit::closure> cobalt_cleaner_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  fidl::BindingSet<ledger_internal::LedgerRepositoryFactory> factory_bindings_;
  fidl::BindingSet<LedgerController> controller_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

void WaitForData() {
  backoff::ExponentialBackoff backoff(zx::msec(10), 2, zx::sec(1));
  zx::time now = zx::clock::get(ZX_CLOCK_MONOTONIC);
  while (zx::clock::get(ZX_CLOCK_MONOTONIC) - now < kMaxPollingDelay) {
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

    FXL_DCHECK(zx::nanosleep(zx::deadline_after(backoff.GetNext())) == ZX_OK);
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
