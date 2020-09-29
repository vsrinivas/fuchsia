// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/bin/sessionmgr/sessionmgr_impl.h"
#include "src/modular/lib/app_driver/cpp/app_driver.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

fit::deferred_action<fit::closure> SetupCobalt(const bool enable_cobalt,
                                               async_dispatcher_t* dispatcher,
                                               sys::ComponentContext* component_context) {
  if (!enable_cobalt) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, component_context);
}

int main(int argc, const char** argv) {
  syslog::SetTags({"sessionmgr"});
  if (!modular::ModularConfigReader::OverriddenConfigExists()) {
    FX_LOGS(WARNING) << "Stopping initialization because a configuration couldn't be found at "
                     << modular::ModularConfigReader::GetOverriddenConfigPath() << ". "
                     << "This is expected if basemgr is shutting down.";
    return 0;
  }

  FX_LOGS(INFO) << "Using configuration at "
                << modular::ModularConfigReader::GetOverriddenConfigPath() << " to start Modular.";

  // Read configurations from file. This sets default values for any
  // configurations that aren't specified in the configuration.
  auto config_reader = modular::ModularConfigReader::CreateFromNamespace();

  fuchsia::modular::session::ModularConfig modular_config;
  modular_config.set_basemgr_config(config_reader.GetBasemgrConfig());
  modular_config.set_sessionmgr_config(config_reader.GetSessionmgrConfig());
  modular::ModularConfigAccessor config_accessor(std::move(modular_config));

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  auto inspector = std::make_unique<sys::ComponentInspector>(component_context.get());
  inspect::Node& inspect_root = inspector->root();

  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto cobalt_cleanup = SetupCobalt(config_accessor.enable_cobalt(), std::move(loop.dispatcher()),
                                    component_context.get());

  modular::AppDriver<modular::SessionmgrImpl> driver(
      component_context->outgoing(),
      std::make_unique<modular::SessionmgrImpl>(component_context.get(), std::move(config_accessor),
                                                std::move(inspect_root)),
      [&loop, cobalt_cleanup = std::move(cobalt_cleanup)]() mutable {
        cobalt_cleanup.call();
        loop.Quit();
      });

  loop.Run();
  return 0;
}
