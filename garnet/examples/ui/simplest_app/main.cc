// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <lib/ui/base_view/cpp/view_provider_component.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <trace-provider/provider.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <cstring>

#include "garnet/examples/ui/simplest_app/view.h"

using namespace simplest_app;

int main(int argc, const char** argv) {
  constexpr char kProcessName[] = "simplest_app";
  zx::process::self()->set_property(ZX_PROP_NAME, kProcessName,
                                    sizeof(kProcessName));

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  FXL_LOG(INFO) << "Using root presenter.";
  FXL_LOG(INFO) << "To quit: Tap the background and hit the ESC key.";

  // We need to attach ourselves to a Presenter. To do this, we create a
  // pair of tokens, and use one to create a View locally (which we attach
  // the rest of our UI to), and one which we pass to a Presenter to create
  // a ViewHolder to embed us.
  //
  // In the Peridot layer of Fuchsia, the device_runner both launches the
  // device shell, and connects it to the root presenter.  Here, we create
  // two eventpair handles, one of which will be passed to the root presenter
  // and the other to the View.
  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

  // Create a startup context for ourselves and use it to connect to
  // environment services.
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();
  fuchsia::ui::scenic::ScenicPtr scenic =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([&loop](zx_status_t status) {
    FXL_LOG(ERROR) << "Lost connection to Scenic with error "
                   << zx_status_get_string(status) << ".";
    loop.Quit();
  });

  // Create a |SimplestAppView| view.
  scenic::ViewContext view_context = {
      .session_and_listener_request =
          scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
      .view_token2 = std::move(view_token),
      .incoming_services = nullptr,
      .outgoing_services = nullptr,
      .startup_context = startup_context.get(),
  };
  auto view = std::make_unique<SimplestAppView>(std::move(view_context), &loop);

  // Display the newly-created view using root_presenter.
  fuchsia::ui::policy::PresenterPtr root_presenter =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>();
  root_presenter->PresentView(std::move(view_holder_token), nullptr);

  loop.Run();
}
