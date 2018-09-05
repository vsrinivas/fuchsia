// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/launch.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "garnet/bin/guest/cli/serial.h"
#include "lib/svc/cpp/services.h"

void handle_launch(int argc, const char* argv[], async::Loop* loop,
                   component::StartupContext* context) {
  // Create environment.
  fuchsia::guest::EnvironmentManagerSyncPtr guestmgr;

  context->ConnectToEnvironmentService(guestmgr.NewRequest());
  fuchsia::guest::EnvironmentControllerSyncPtr guest_env;
  guestmgr->Create(argv[0], guest_env.NewRequest());

  // Launch guest.
  fuchsia::guest::InstanceControllerPtr guest_controller;
  fuchsia::guest::LaunchInfo launch_info;
  launch_info.url = argv[0];
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.args.push_back(argv[i + 1]);
  }
  fuchsia::guest::InstanceInfo guest_info;
  guest_env->LaunchInstance(std::move(launch_info), guest_controller.NewRequest(),
                         &guest_info);
  guest_controller.set_error_handler([loop] { loop->Shutdown(); });

  // Create the framebuffer view.
  guest_controller->GetViewProvider([context](auto view_provider) {
    if (!view_provider.is_valid()) {
      return;
    }
    auto view_provider_ptr = view_provider.Bind();

    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner;
    view_provider_ptr->CreateView(view_owner.NewRequest(), nullptr);

    // Ask the presenter to display it.
    fuchsia::ui::policy::PresenterSyncPtr presenter;
    context->ConnectToEnvironmentService(presenter.NewRequest());
    presenter->Present(std::move(view_owner), nullptr);
  });

  // Open the serial service of the guest and process IO.
  zx::socket socket;
  SerialConsole console(loop);
  guest_controller->GetSerial(
      [&console](zx::socket socket) { console.Start(std::move(socket)); });
  loop->Run();
}
