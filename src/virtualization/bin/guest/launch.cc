// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/launch.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <unordered_map>

#include "fuchsia/virtualization/cpp/fidl.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/guest/serial.h"

namespace {

const std::unordered_map<std::string, std::string> kGuestTypes = {
    {"zircon", "fuchsia-pkg://fuchsia.com/zircon_guest#meta/zircon_guest.cmx"},
    {"debian", "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx"},
    {"termina", "fuchsia-pkg://fuchsia.com/termina_guest#meta/termina_guest.cmx"},
};

void PrintSupportedGuests(const char* guest) {
  fprintf(stderr, "Unrecognized guest type: %s. Supported guests:\n", guest);
  for (const auto& it : kGuestTypes) {
    fprintf(stderr, "  %s\n", it.first.c_str());
  }
}

std::string GetGuestUrl(const std::string& guest) {
  auto it = kGuestTypes.find(guest);
  return it == kGuestTypes.end() ? "" : it->second;
}

}  // namespace

zx_status_t handle_launch(int argc, const char** argv, async::Loop* loop,
                          fuchsia::virtualization::GuestConfig cfg,
                          sys::ComponentContext* context) {
  std::string guest_url = GetGuestUrl(argv[0]);
  if (guest_url.empty()) {
    PrintSupportedGuests(argv[0]);
    return ZX_ERR_INVALID_ARGS;
  }

  fprintf(stdout, "Starting %s with package %s.\n", argv[0], guest_url.c_str());

  // Create environment.
  fuchsia::virtualization::ManagerPtr manager;
  zx_status_t status = context->svc()->Connect(manager.NewRequest());
  if (status != ZX_OK) {
    printf("Failed to connect to guest manager: %s\n", zx_status_get_string(status));
    return status;
  }
  fuchsia::virtualization::RealmPtr realm;
  manager->Create(argv[0], realm.NewRequest());

  // Launch guest.
  fuchsia::virtualization::GuestPtr guest;
  realm->LaunchInstance(guest_url, cpp17::nullopt, std::move(cfg), guest.NewRequest(),
                        [](uint32_t) {});

  // Set up error handling.
  guest.set_error_handler([&loop](zx_status_t status) {
    fprintf(stderr, "Connection to guest closed: %s\n", zx_status_get_string(status));
    loop->Quit();
  });

  // Set up serial output.
  OutputWriter serial(loop);
  guest->GetSerial([&loop, &serial](fuchsia::virtualization::Guest_GetSerial_Result result) {
    if (result.is_err()) {
      fprintf(stderr, "Could not connect to guest serial: %s\n",
              zx_status_get_string(result.err()));
      loop->Quit();
      return;
    }
    serial.Start(std::move(result.response()).socket);
  });

  // Set up guest console.
  GuestConsole console(loop);
  guest->GetConsole([&loop, &console](fuchsia::virtualization::Guest_GetConsole_Result result) {
    if (result.is_err()) {
      fprintf(stderr, "Could not connect to guest console: %s.\n",
              zx_status_get_string(result.err()));
      loop->Quit();
      return;
    }
    console.Start(std::move(result.response()).socket);
  });

  return loop->Run();
}
