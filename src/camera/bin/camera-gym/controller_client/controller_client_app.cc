// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller_client_app.h"

#include <glob.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <re2/re2.h>

#include "src/camera/bin/camera-gym/controller_error/controller_error.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

namespace camera {

// This path will exist when the tool is used in combination with `ffx component explore`.
static char kServicePath[] = "/out/svc/fuchsia.camera.gym.Controller";

ControllerClientApp::ControllerClientApp(/* const char* name, */ async::Loop& loop)
    : loop_(loop), context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

void ControllerClientApp::Start(std::vector<fuchsia::camera::gym::Command> commands) {
  ConnectToServer();
  SendCommand(std::move(commands), 0);
}

bool ControllerClientApp::ConnectToServer() {
  auto request = controller_.NewRequest().TakeChannel();
  const zx_status_t status = fdio_service_connect(kServicePath, request.release());
  if (status != ZX_OK) {
    fprintf(stderr, "ERROR: FDIO service connect failure (status: %s)\n",
            zx_status_get_string(status));
    return false;
  }
  return true;
}

void ControllerClientApp::SendCommand(std::vector<fuchsia::camera::gym::Command> commands,
                                      uint32_t index) {
  fuchsia::camera::gym::Command command = std::move(commands[index]);
  controller_->SendCommand(
      std::move(command), [this, commands = std::move(commands), index](
                              fuchsia::camera::gym::Controller_SendCommand_Result result) mutable {
        if (result.is_err()) {
          auto str = CommandErrorString(result.err());
          fprintf(stderr, "Command error: %s (%u)\n", str.c_str(), result.err());
          loop_.Quit();
          return;
        }
        if (!result.is_response()) {
          fprintf(stderr, "ERROR: Invalid return\n");
          loop_.Quit();
          return;
        }
        fprintf(stderr, "OK\n");

        uint32_t next_index = index + 1;
        if (next_index == commands.size()) {
          loop_.Quit();
          return;
        }

        // Execute next command
        SendCommand(std::move(commands), next_index);
      });
}

void ControllerClientApp::Quit() {
  loop_.Quit();
  loop_.JoinThreads();
}

}  // namespace camera
