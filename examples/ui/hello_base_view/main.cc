// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/examples/ui/hello_base_view/example_presenter.h"
#include "garnet/examples/ui/hello_base_view/view.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/ui/base_view/cpp/view_provider_service.h"

using namespace hello_base_view;

int main(int argc, const char** argv) {
  FXL_LOG(INFO) << "Launching hello_base_view!";

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const bool kUseRootPresenter = command_line.HasOption("use_root_presenter");
  const bool kUseExamplePresenter =
      command_line.HasOption("use_example_presenter");

  if (kUseRootPresenter && kUseExamplePresenter) {
    FXL_LOG(ERROR)
        << "Cannot set both --use_root_presenter and --use_example_presenter";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  fidl::InterfacePtr<fuchsia::ui::scenic::Scenic> scenic =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic.set_error_handler([&loop] {
    FXL_LOG(INFO) << "Lost connection to Scenic.";
    loop.Quit();
  });

  // We need to attach ourselves to a Presenter. To do this, we create a pair of
  // tokens, and use one to create a View locally (which we attach the rest of
  // our UI to), and one which we pass to a Presenter to create a ViewHolder to
  // embed us.
  //
  // In the Peridot layer of Fuchsia, the basemgr both launches the device
  // shell, and connects it to the root presenter.  Here, we create two
  // eventpair handles, one of which will be passed to our example Presenter and
  // the other to the View.
  //
  // For simplicity, both the presenter and the view run in-process, and the
  // tokens are passed to them via C++ methods.  However, it would work just as
  // well if the presenter/view lived in two other processes, and we passed the
  // tokens to them via FIDL messages.  In Peridot, this is exactly what the
  // basemgr does.
  zx::eventpair view_holder_token, view_token;
  if (ZX_OK != zx::eventpair::create(0u, &view_holder_token, &view_token)) {
    FXL_LOG(ERROR) << "hello_base_view: parent failed to create tokens.";
    return 1;
  }

  if (kUseRootPresenter) {
    FXL_LOG(INFO) << "Using root presenter.";
    FXL_LOG(INFO) << "To quit: Tap the background and hit the ESC key.";

    // Create a View; Shadertoy's View will be plumbed through it.
    scenic::ViewContext view_context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token = std::move(view_token),
        .incoming_services = nullptr,
        .outgoing_services = nullptr,
        .startup_context = startup_context.get(),
    };
    auto view =
        std::make_unique<ShadertoyEmbedderView>(std::move(view_context), &loop);

    fuchsia::ui::policy::Presenter2Ptr root_presenter =
        startup_context
            ->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter2>();
    root_presenter->PresentView(std::move(view_holder_token), nullptr);

    // Launch the real shadertoy and attach its View to this View.
    view->LaunchShadertoyClient();
    loop.Run();

  } else if (kUseExamplePresenter) {
    FXL_LOG(INFO) << "Using example presenter.";

    // Create a View; Shadertoy's View will be plumbed through it.
    scenic::ViewContext view_context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token = std::move(view_token),
        .incoming_services = nullptr,
        .outgoing_services = nullptr,
        .startup_context = startup_context.get(),
    };
    auto view =
        std::make_unique<ShadertoyEmbedderView>(std::move(view_context), &loop);

    // N.B. The example presenter has an independent session to Scenic.
    auto example_presenter = std::make_unique<ExamplePresenter>(scenic.get());
    // This would typically be done by the root Presenter.
    scenic->GetDisplayInfo(
        [&example_presenter, view_holder_token = std::move(view_holder_token)](
            fuchsia::ui::gfx::DisplayInfo display_info) mutable {
          example_presenter->Init(
              static_cast<float>(display_info.width_in_px),
              static_cast<float>(display_info.height_in_px));
          example_presenter->PresentView(std::move(view_holder_token), nullptr);
        });

    // Launch the real shadertoy and attach its View to this View.
    view->LaunchShadertoyClient();
    loop.Run();

  } else {
    // Instead of creating a View directly, provide a service that will do so.
    FXL_LOG(INFO) << "Launching view provider service.";
    auto view_provider_service = std::make_unique<scenic::ViewProviderService>(
        startup_context.get(), scenic.get(),
        [&loop](scenic::ViewContext context) {
          // Create a View; Shadertoy's View will be plumbed through it.
          auto view = std::make_unique<ShadertoyEmbedderView>(
              std::move(context), &loop);

          // Launch the real shadertoy and attach its View to this View.
          view->LaunchShadertoyClient();
          return view;
        });
    loop.Run();
  }

  return 0;
}
