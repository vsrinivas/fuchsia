// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include <trace-provider/provider.h>

#include "src/lib/fxl/macros.h"
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
  // Read configurations from file. This sets default values for any
  // configurations that aren't specified in the configuration.
  auto config_reader = modular::ModularConfigReader::CreateFromNamespace();
  fuchsia::modular::session::SessionmgrConfig config = config_reader.GetSessionmgrConfig();

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto component_context = sys::ComponentContext::Create();
  auto inspector = std::make_unique<sys::ComponentInspector>(component_context.get());
  inspect::Node& inspect_root = inspector->root();

  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto cobalt_cleanup =
      SetupCobalt((config.enable_cobalt()), std::move(loop.dispatcher()), component_context.get());

  modular::AppDriver<modular::SessionmgrImpl> driver(
      component_context->outgoing(),
      std::make_unique<modular::SessionmgrImpl>(component_context.get(), std::move(config),
                                                std::move(inspect_root)),
      [&loop, cobalt_cleanup = std::move(cobalt_cleanup)]() mutable {
        cobalt_cleanup.call();
        loop.Quit();
      });

  loop.Run();
  return 0;
}
