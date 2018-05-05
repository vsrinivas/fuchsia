// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/launch.h"

#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/presentation.h>
#include <fuchsia/cpp/views_v1.h>

#include "garnet/bin/guest/cli/serial.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/svc/cpp/services.h"

void handle_launch(int argc, const char* argv[]) {
  // Create environment.
  guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());
  guest::GuestEnvironmentSyncPtr guest_env;
  guestmgr->CreateEnvironment(argv[0], guest_env.NewRequest());

  // Launch guest.
  guest::GuestLaunchInfo launch_info;
  launch_info.url = argv[0];
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.vmm_args.push_back(argv[i + 1]);
  }
  guest_env->LaunchGuest(std::move(launch_info),
                         g_guest_controller.NewRequest());

  // Create the framebuffer view.
  views_v1::ViewProviderSyncPtr view_provider;
  g_guest_controller->FetchViewProvider(view_provider.NewRequest());
  fidl::InterfaceHandle<views_v1_token::ViewOwner> view_owner;
  view_provider->CreateView(view_owner.NewRequest(), nullptr);

  // Ask the presenter to display it.
  presentation::PresenterSyncPtr presenter;
  component::ConnectToEnvironmentService(presenter.NewRequest());
  presenter->Present(std::move(view_owner), nullptr);

  // Open the serial service of the guest and process IO.
  handle_serial(g_guest_controller.get());
}
