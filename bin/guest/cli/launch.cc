// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/launch.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "garnet/bin/guest/cli/serial.h"
#include "lib/component/cpp/environment_services.h"
#include "lib/svc/cpp/services.h"

void handle_launch(int argc, const char* argv[]) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  // Create environment.
  fuchsia::guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());
  fuchsia::guest::GuestEnvironmentSyncPtr guest_env;
  guestmgr->CreateEnvironment(argv[0], guest_env.NewRequest());

  // Launch guest.
  fuchsia::guest::GuestControllerPtr guest_controller;
  fuchsia::guest::GuestLaunchInfo launch_info;
  launch_info.url = argv[0];
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.vmm_args.push_back(argv[i + 1]);
  }
  fuchsia::guest::GuestInfo guest_info;
  guest_env->LaunchGuest(std::move(launch_info), guest_controller.NewRequest(),
                         &guest_info);
  guest_controller.set_error_handler([&loop] { loop.Shutdown(); });

  // Create the framebuffer view.
  guest_controller->GetViewProvider([](auto view_provider) {
    if (!view_provider.is_valid()) {
      return;
    }
    auto view_provider_ptr = view_provider.Bind();

    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner;
    view_provider_ptr->CreateView(view_owner.NewRequest(), nullptr);

    // Ask the presenter to display it.
    fuchsia::ui::policy::PresenterSyncPtr presenter;
    component::ConnectToEnvironmentService(presenter.NewRequest());
    presenter->Present(std::move(view_owner), nullptr);
  });

  // Open the serial service of the guest and process IO.
  zx::socket socket;
  SerialConsole console(&loop);
  guest_controller->GetSerial(
      [&console](zx::socket socket) { console.Start(std::move(socket)); });
  loop.Run();
}
