// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/launch.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "src/virtualization/bin/guest/serial.h"

void handle_launch(int argc, const char* argv[], async::Loop* loop,
                   sys::ComponentContext* context) {
  // Create environment.
  fuchsia::virtualization::ManagerPtr manager;
  context->svc()->Connect(manager.NewRequest());
  fuchsia::virtualization::RealmPtr realm;
  manager->Create(argv[0], realm.NewRequest());

  // Launch guest.
  fuchsia::virtualization::LaunchInfo launch_info;
  launch_info.url = fxl::StringPrintf(
      "fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx", argv[0], argv[0]);
  for (int i = 0; i < argc - 1; ++i) {
    launch_info.args.push_back(argv[i + 1]);
  }
  fuchsia::virtualization::GuestPtr guest;
  realm->LaunchInstance(std::move(launch_info), guest.NewRequest(), [](...) {});

  // Setup serial console.
  SerialConsole console(loop);
  guest->GetSerial(
      [&console](zx::socket socket) { console.Start(std::move(socket)); });

  loop->Run();
}
