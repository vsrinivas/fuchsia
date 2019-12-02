// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <unistd.h>
#include <zircon/device/vfs.h>

#include <memory>
#include <utility>

#include <trace-provider/provider.h>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/cobalt/cobalt.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_factory_impl.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/inspect_deprecated/deprecated/object_dir.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/abseil-cpp/absl/flags/parse.h"

ABSL_FLAG(bool, disable_reporting, false, "disable sending statistics to Cobalt");
ABSL_FLAG(bool, disable_p2p_sync, false, "disable peer-to-peer syncing");
ABSL_FLAG(int, verbose, 0, "level of verbosity");
ABSL_FLAG(int, v, 0, "alias for verbose");

namespace ledger {
namespace {

struct AppParams {
  bool disable_statistics = false;
  bool disable_p2p_sync = false;
  storage::GarbageCollectionPolicy gc_policy = kDefaultGarbageCollectionPolicy;
};

struct InspectObjects {
  inspect_deprecated::Node top_level_node;
  inspect_deprecated::StringProperty statistic_gathering;
};

fit::deferred_action<fit::closure> SetupCobalt(bool disable_statistics,
                                               async_dispatcher_t* dispatcher,
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
        loop_(&kAsyncLoopConfigAttachToCurrentThread),
        io_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        trace_provider_(loop_.dispatcher()),
        component_context_(sys::ComponentContext::Create()),
        cobalt_cleaner_(SetupCobalt(app_params_.disable_statistics, loop_.dispatcher(),
                                    component_context_.get())),
        factory_bindings_(loop_.dispatcher()) {
    FXL_DCHECK(component_context_);

    ReportEvent(CobaltEvent::LEDGER_STARTED);
  }
  ~App() override = default;

  bool Start() {
    io_loop_.StartThread("io thread");
    auto objects = component::Object::Make(kTopLevelNodeName.ToString());
    auto object_dir = component::ObjectDir(objects);

    component_context_->outgoing()
        ->GetOrCreateDirectory(kInspectNodesDirectory.ToString())
        ->AddEntry(fuchsia::inspect::deprecated::Inspect::Name_,
                   std::make_unique<vfs::Service>(
                       inspect_bindings_.GetHandler(object_dir.object().get())));
    inspect_objects_.top_level_node = inspect_deprecated::Node(std::move(object_dir));
    inspect_objects_.statistic_gathering = inspect_objects_.top_level_node.CreateStringProperty(
        "statistic_gathering", app_params_.disable_statistics ? "off" : "on");

    EnvironmentBuilder builder;

    environment_ =
        std::make_unique<Environment>(builder.SetDisableStatistics(app_params_.disable_statistics)
                                          .SetAsync(loop_.dispatcher())
                                          .SetIOAsync(io_loop_.dispatcher())
                                          .SetStartupContext(component_context_.get())
                                          .SetGcPolicy(app_params_.gc_policy)
                                          .Build());
    std::unique_ptr<p2p_sync::UserCommunicatorFactoryImpl> user_communicator_factory;
    if (!app_params_.disable_p2p_sync) {
      user_communicator_factory =
          std::make_unique<p2p_sync::UserCommunicatorFactoryImpl>(environment_.get());
    }

    factory_impl_ = std::make_unique<LedgerRepositoryFactoryImpl>(
        environment_.get(), std::move(user_communicator_factory),
        inspect_objects_.top_level_node.CreateChild(kRepositoriesInspectPathComponent.ToString()));

    component_context_->outgoing()->AddPublicService<ledger_internal::LedgerRepositoryFactory>(
        [this](fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory> request) {
          factory_bindings_.emplace(factory_impl_.get(), std::move(request));
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
  fidl::BindingSet<fuchsia::inspect::deprecated::Inspect> inspect_bindings_;
  async::Loop loop_;
  async::Loop io_loop_;
  trace::TraceProviderWithFdio trace_provider_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  fit::deferred_action<fit::closure> cobalt_cleaner_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  callback::AutoCleanableSet<
      SyncableBinding<fuchsia::ledger::internal::LedgerRepositoryFactorySyncableDelegate>>
      factory_bindings_;
  fidl::BindingSet<LedgerController> controller_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

int Main(int argc, char** argv) {
  // TODO(qsr): Remove when moving logging outside of fxl.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  absl::ParseCommandLine(argc, argv);

  AppParams app_params;
  app_params.disable_statistics = absl::GetFlag(FLAGS_disable_reporting);
  app_params.disable_p2p_sync = absl::GetFlag(FLAGS_disable_p2p_sync);
  app_params.gc_policy = absl::GetFlag(FLAGS_gc_policy);

  App app(app_params);
  if (!app.Start()) {
    return 1;
  }

  return 0;
}

}  // namespace
}  // namespace ledger

int main(int argc, char** argv) { return ledger::Main(argc, argv); }
