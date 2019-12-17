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
#include <lib/trace-provider/provider.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/p2p_provider/public/p2p_provider_factory.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/abseil-cpp/absl/flags/parse.h"

ABSL_FLAG(bool, disable_p2p_sync, false, "disable peer-to-peer syncing");
ABSL_FLAG(int, verbose, 0, "level of verbosity");

namespace ledger {
namespace {

struct AppParams {
  bool disable_statistics = false;
  bool disable_p2p_sync = false;
  storage::GarbageCollectionPolicy gc_policy = kDefaultGarbageCollectionPolicy;
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
        factory_bindings_(loop_.dispatcher()) {
    LEDGER_DCHECK(component_context_);
  }
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App() override = default;

  bool Start() {
    io_loop_.StartThread("io thread");

    EnvironmentBuilder builder;

    environment_ =
        std::make_unique<Environment>(builder.SetDisableStatistics(app_params_.disable_statistics)
                                          .SetAsync(loop_.dispatcher())
                                          .SetIOAsync(io_loop_.dispatcher())
                                          .SetStartupContext(component_context_.get())
                                          .SetGcPolicy(app_params_.gc_policy)
                                          .Build());

    factory_impl_ = std::make_unique<LedgerRepositoryFactoryImpl>(environment_.get(), nullptr);

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
  async::Loop loop_;
  async::Loop io_loop_;
  trace::TraceProviderWithFdio trace_provider_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  std::unique_ptr<Environment> environment_;
  std::unique_ptr<LedgerRepositoryFactoryImpl> factory_impl_;
  AutoCleanableSet<
      SyncableBinding<fuchsia::ledger::internal::LedgerRepositoryFactorySyncableDelegate>>
      factory_bindings_;
  fidl::BindingSet<LedgerController> controller_bindings_;
};

int Main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  SetLogVerbosity(absl::GetFlag(FLAGS_verbose));

  AppParams app_params;
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
