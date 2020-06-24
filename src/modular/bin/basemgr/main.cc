// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/macros.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/macros.h"
#include "src/modular/bin/basemgr/basemgr_impl.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace {
#ifdef AUTO_LOGIN_TO_GUEST
constexpr bool kStableSessionId = true;
#else
constexpr bool kStableSessionId = false;
#endif

}  // namespace

fit::deferred_action<fit::closure> SetupCobalt(bool enable_cobalt, async_dispatcher_t* dispatcher,
                                               sys::ComponentContext* component_context) {
  if (!enable_cobalt) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, component_context);
};

// Configures Basemgr by passing in connected services.
std::unique_ptr<modular::BasemgrImpl> ConfigureBasemgr(
    fuchsia::modular::session::ModularConfig& config, sys::ComponentContext* component_context,
    async::Loop* loop) {
  fit::deferred_action<fit::closure> cobalt_cleanup =
      SetupCobalt(config.basemgr_config().enable_cobalt(), loop->dispatcher(), component_context);

  return std::make_unique<modular::BasemgrImpl>(
      std::move(config), component_context->svc(), component_context->outgoing(),
      component_context->svc()->Connect<fuchsia::sys::Launcher>(),
      component_context->svc()->Connect<fuchsia::ui::policy::Presenter>(),
      component_context->svc()->Connect<fuchsia::devicesettings::DeviceSettingsManager>(),
      component_context->svc()->Connect<fuchsia::wlan::service::Wlan>(),
      component_context->svc()->Connect<fuchsia::hardware::power::statecontrol::Admin>(),
      /*on_shutdown=*/
      [loop, cobalt_cleanup = std::move(cobalt_cleanup), component_context]() mutable {
        cobalt_cleanup.call();
        component_context->outgoing()->debug_dir()->RemoveEntry(modular_config::kBasemgrConfigName);
        loop->Quit();
      });
}

// Delegates lifecycle requests to BasemgrImpl if available. Otherwise, exits the given async loop.
class LifecycleImpl : public fuchsia::modular::Lifecycle {
 public:
  LifecycleImpl(async::Loop* loop, sys::ComponentContext* component_context) : loop_(loop) {
    component_context->outgoing()->AddPublicService<fuchsia::modular::Lifecycle>(
        bindings_.GetHandler(this));
  }

  void set_sink(modular::BasemgrImpl* basemgr) { basemgr_ = basemgr; }

 private:
  // |fuchsia::modular::Lifecycle|
  void Terminate() override {
    if (basemgr_) {
      basemgr_->Terminate();
    } else {
      loop_->Quit();
    }
  }

  modular::BasemgrImpl* basemgr_ = nullptr;
  async::Loop* loop_;
  fidl::BindingSet<fuchsia::modular::Lifecycle> bindings_;
};

int main(int argc, const char** argv) {
  syslog::SetTags({"basemgr"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fuchsia::modular::session::ModularConfig modular_config;

  if (argc == 1) {
    // Read configurations from file if no command line arguments are passed in.
    // This sets default values for any configurations that aren't specified in
    // the configuration file.
    auto config_reader = modular::ModularConfigReader::CreateFromNamespace();
    modular_config.set_basemgr_config(config_reader.GetBasemgrConfig());
    modular_config.set_sessionmgr_config(config_reader.GetSessionmgrConfig());
  } else {
    std::cerr << "basemgr does not support arguments. Please use basemgr_launcher to "
              << "launch basemgr with custom configurations." << std::endl;
    return 1;
  }

  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> component_context(
      sys::ComponentContext::CreateAndServeOutgoingDirectory());

  std::unique_ptr<modular::BasemgrImpl> basemgr_impl;
  LifecycleImpl lifecycle_impl(&loop, component_context.get());

  // Clients can specify whether a stable session ID is used by setting a
  // property against base shell in the modular config. Parse the
  // |auto_login_to_guest| build flag to set the same property.
  if (kStableSessionId) {
    FX_LOGS(INFO) << "Requesting stable session ID based on build flag";
    fuchsia::modular::session::AppConfig override_base_shell;
    override_base_shell.mutable_args()->push_back("--persist_user");
    modular_config.mutable_basemgr_config()->mutable_base_shell()->set_app_config(
        std::move(override_base_shell));
  }

  std::unique_ptr<modular::BasemgrImpl> basemgr =
      ConfigureBasemgr(modular_config, component_context.get(), &loop);

  basemgr_impl = std::move(basemgr);
  lifecycle_impl.set_sink(basemgr_impl.get());

  // NOTE: component_controller.events.OnDirectoryReady() is triggered when a
  // component's out directory has mounted. basemgr_launcher uses this signal
  // to determine when basemgr has completed initialization so it can detach
  // and stop itself. When basemgr_launcher is used, it's responsible for
  // providing basemgr a configuration file. To ensure we don't shutdown
  // basemgr_launcher too early, we need additions to out/ to complete after
  // configurations have been parsed.
  component_context->outgoing()->debug_dir()->AddEntry(
      modular_config::kBasemgrConfigName,
      std::make_unique<vfs::Service>([basemgr_impl = basemgr_impl.get()](zx::channel request,
                                                                         async_dispatcher_t*) {
        basemgr_impl->Connect(
            fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug>(std::move(request)));
      }));

  loop.Run();

  return 0;
}
