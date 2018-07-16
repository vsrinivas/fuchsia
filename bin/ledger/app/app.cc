// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <utility>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <trace-provider/provider.h>
#include <zircon/device/vfs.h>

#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/cobalt/cobalt.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_factory_impl.h"

namespace ledger {

namespace {
constexpr fxl::StringView kNoStatisticsReporting = "disable_reporting";

struct AppParams {
  bool disable_statistics = false;
};

fxl::AutoCall<fit::closure> SetupCobalt(
    bool disable_statistics, async_dispatcher_t* dispatcher,
    component::StartupContext* startup_context) {
  if (disable_statistics) {
    return fxl::MakeAutoCall<fit::closure>([] {});
  }
  return InitializeCobalt(dispatcher, startup_context);
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
        trace_provider_(loop_.dispatcher()),
        startup_context_(component::StartupContext::CreateFromStartupInfo()),
        cobalt_cleaner_(SetupCobalt(app_params_.disable_statistics,
                                    loop_.dispatcher(), startup_context_.get())) {
    FXL_DCHECK(startup_context_);

    ReportEvent(CobaltEvent::LEDGER_STARTED);
  }
  ~App() override {}

  bool Start() {
    io_loop_.StartThread("io thread");
    environment_ =
        std::make_unique<Environment>(EnvironmentBuilder()
                                          .SetAsync(loop_.dispatcher())
                                          .SetIOAsync(io_loop_.dispatcher())
                                          .Build());
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
  async::Loop io_loop_;
  trace::TraceProvider trace_provider_;
  std::unique_ptr<component::StartupContext> startup_context_;
  fxl::AutoCall<fit::closure> cobalt_cleaner_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  fidl::BindingSet<ledger_internal::LedgerRepositoryFactory> factory_bindings_;
  fidl::BindingSet<LedgerController> controller_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  ledger::AppParams app_params;
  app_params.disable_statistics =
      command_line.HasOption(ledger::kNoStatisticsReporting);

  ledger::App app(app_params);
  if (!app.Start()) {
    return 1;
  }

  return 0;
}
