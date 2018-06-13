// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/examples/ui/hello_base_view/example_presenter.h"
#include "garnet/examples/ui/hello_base_view/view.h"

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"

using namespace hello_base_view;

int main(int argc, const char** argv) {
  FXL_LOG(INFO) << "Launching hello_base_view!";

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto startup_context = fuchsia::sys::StartupContext::CreateFromStartupInfo();

  auto scenic =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([&loop] {
    FXL_LOG(INFO) << "Lost connection to Scenic.";
    loop.Quit();
  });

  // TODO(SCN-805): We run an implementation of Presenter in-process, which
  // talks directly to Scenic (not RootPresenter).  This means that
  // hello_base_view cannot be run as a normal embedded app.  The purpose of
  // this is to illustrate something analogous to how, at the Peridot layer of
  // Fuchsia, the device_runner hooks up the device_shell to the root presenter.
  //
  // NOTE: although the presenter and the view share the same Scenic, each of
  // them creates their own Scenic Session.
  ExamplePresenter presenter(scenic.get());

  // We need to attach ourselves to a Presenter. To do this, we create a pair of
  // tokens, and use one to create a View locally (which we attach the rest of
  // our UI to), and one which we pass to a Presenter to create a ViewHolder to
  // embed us.
  //
  // In the Peridot layer of Fuchsia, the device_runner both launches the device
  // shell, and connects it to the root presenter.  Here, we create two
  // eventpair handles, one of which will be passed to our example Presenter and
  // the other to the View.
  //
  // For simplicity, both the presenter and the view run in-process, and the
  // tokens are passed to them via C++ methods.  However, it would work just as
  // well if the presenter/view lived in two other processes, and we passed the
  // tokens to them via FIDL messages.  In Peridot, this is exactly what the
  // device_runner does.
  zx::eventpair view_holder_token, view_token;
  if (ZX_OK != zx::eventpair::create(0u, &view_holder_token, &view_token)) {
    FXL_LOG(ERROR) << "hello_base_view: parent failed to create tokens.";
    return 1;
  }

  // This would typically be done by the root Presenter.
  scenic->GetDisplayInfo(
      [&presenter, token{std::move(view_holder_token)}](
          fuchsia::ui::gfx::DisplayInfo display_info) mutable {
        presenter.Init(static_cast<float>(display_info.width_in_px),
                       static_cast<float>(display_info.height_in_px));
        presenter.PresentView(std::move(token), nullptr);
      });

  // As described above, instead of launching the view in another app we create
  // it here and pass it the view token (which will be used to create a Scenic
  // View resource that corresponds to the presenter's ViewHolder).
  ShadertoyEmbedderView view(startup_context.get(), scenic.get(),
                             std::move(view_token));

  // ShadertoyEmbedderView inherits from scenic::BaseView, which makes it easy
  // to launch and embed child apps.
  view.LaunchShadertoyClient();

  loop.Run();
  return 0;
}
