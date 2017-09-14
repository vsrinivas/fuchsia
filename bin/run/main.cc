// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fdio/util.h>

#include "lib/app/fidl/application_controller.fidl-sync.h"
#include "lib/app/fidl/application_launcher.fidl-sync.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"

int main(int argc, const char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: run <program> <args>*\n");
    return 1;
  }
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = argv[1];
  for (int i = 0; i < argc - 2; ++i) {
    launch_info->arguments.push_back(argv[2 + i]);
  }

  // Manually connect to the service root instead of using ApplicationContext to
  // avoid having to spin up a message loop just to send 2 messages.
  zx::channel h1, service_root;
  if (zx::channel::create(0, &h1, &service_root) != ZX_OK)
    return 1;

  // TODO(abarth): Use "/svc/" once that actually works.
  if (fdio_service_connect("/svc/.", h1.release()) != ZX_OK)
    return 1;

  fidl::SynchronousInterfacePtr<app::ApplicationLauncher> launcher;
  auto launcher_request = GetSynchronousProxy(&launcher);
  fdio_service_connect_at(service_root.get(), launcher->Name_,
                          launcher_request.PassChannel().release());

  fidl::SynchronousInterfacePtr<app::ApplicationController> controller;
  auto controller_request = GetSynchronousProxy(&controller);
  launcher->CreateApplication(std::move(launch_info), std::move(controller_request));

  int32_t return_code;
  if (!controller->Wait(&return_code)) {
    fprintf(stderr, "%s exited without a return code\n", argv[1]);
    return 1;
  }
  return return_code;
}
