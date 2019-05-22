// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/inspect/deprecated/object_dir.h>
#include <lib/inspect/inspect.h>
#include <lib/sys/cpp/component_context.h>
#include <trace-provider/provider.h>
#include <unistd.h>
#include <zircon/device/vfs.h>

#include <memory>
#include <utility>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/cobalt/cobalt.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/error_notifier.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_factory_impl.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace ledger {
namespace {

constexpr fxl::StringView kNoStatisticsReportingFlag = "disable_reporting";
constexpr fxl::StringView kNoPeerToPeerSync = "disable_p2p_sync";
constexpr fxl::StringView kFirebaseApiKeyFlag = "firebase_api_key";

struct AppParams {
  bool disable_statistics = false;
  bool disable_p2p_sync = false;
  std::string firebase_api_key = "";
};

struct InspectObjects {
  inspect::Node top_level_node;
  inspect::StringProperty statistic_gathering;
};

fit::deferred_action<fit::closure> SetupCobalt(
    bool disable_statistics, async_dispatcher_t* dispatcher,
    sys::ComponentContext* component_context) {
  if (disable_statistics) {
    return fit::defer<fit::closure>([] {});
  }
  return InitializeCobalt(dispatcher, component_context);
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
      : app_params_(std::move(app_params)),
        loop_(&kAsyncLoopConfigAttachToThread),
        io_loop_(&kAsyncLoopConfigNoAttachToThread),
        trace_provider_(loop_.dispatcher()),
        component_context_(sys::ComponentContext::Create()),
        cobalt_cleaner_(SetupCobalt(app_params_.disable_statistics,
                                    loop_.dispatcher(),
                                    component_context_.get())) {
    FXL_DCHECK(component_context_);

    ReportEvent(CobaltEvent::LEDGER_STARTED);
  }
  ~App() override {}

  bool Start() {
    io_loop_.StartThread("io thread");
    auto objects = component::Object::Make(kTopLevelNodeName);
    auto object_dir = component::ObjectDir(objects);

    component_context_->outgoing()
        ->GetOrCreateDirectory(kInspectNodesDirectory)
        ->AddEntry(fuchsia::inspect::Inspect::Name_,
                   std::make_unique<vfs::Service>(inspect_bindings_.GetHandler(
                       object_dir.object().get())));
    inspect_objects_.top_level_node = inspect::Node(std::move(object_dir));
    inspect_objects_.statistic_gathering =
        inspect_objects_.top_level_node.CreateStringProperty(
            "statistic_gathering",
            app_params_.disable_statistics ? "off" : "on");

    EnvironmentBuilder builder;

    if (!app_params_.firebase_api_key.empty()) {
      builder.SetFirebaseApiKey(app_params_.firebase_api_key);
    }

    environment_ = std::make_unique<Environment>(
        builder.SetDisableStatistics(app_params_.disable_statistics)
            .SetAsync(loop_.dispatcher())
            .SetIOAsync(io_loop_.dispatcher())
            .SetStartupContext(component_context_.get())
            .Build());
    std::unique_ptr<p2p_sync::UserCommunicatorFactoryImpl>
        user_communicator_factory;
    if (!app_params_.disable_p2p_sync) {
      user_communicator_factory =
          std::make_unique<p2p_sync::UserCommunicatorFactoryImpl>(
              environment_.get());
    }

    factory_impl_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        environment_.get(), std::move(user_communicator_factory),
        inspect_objects_.top_level_node.CreateChild(
            kRepositoriesInspectPathComponent));

    component_context_->outgoing()
        ->AddPublicService<ledger_internal::LedgerRepositoryFactory>(
            [this](
                fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
                    request) {
              factory_bindings_.emplace(factory_impl_.get(),
                                        std::move(request));
            });
    component_context_->outgoing()->AddPublicService<LedgerController>(
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
  InspectObjects inspect_objects_;
  fidl::BindingSet<fuchsia::inspect::Inspect> inspect_bindings_;
  async::Loop loop_;
  async::Loop io_loop_;
  trace::TraceProvider trace_provider_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  fit::deferred_action<fit::closure> cobalt_cleaner_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  callback::AutoCleanableSet<ErrorNotifierBinding<
      fuchsia::ledger::internal::LedgerRepositoryFactoryErrorNotifierDelegate>>
      factory_bindings_;
  fidl::BindingSet<LedgerController> controller_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

int Main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  AppParams app_params;
  app_params.disable_statistics =
      command_line.HasOption(kNoStatisticsReportingFlag);
  app_params.disable_p2p_sync = command_line.HasOption(kNoPeerToPeerSync);
  if (command_line.HasOption(kFirebaseApiKeyFlag)) {
    if (!command_line.GetOptionValue(kFirebaseApiKeyFlag,
                                     &app_params.firebase_api_key)) {
      FXL_LOG(ERROR) << "Unable to retrieve the firebase api key.";
      return 1;
    }
  }

  App app(app_params);
  if (!app.Start()) {
    return 1;
  }

  return 0;
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) { return ledger::Main(argc, argv); }
