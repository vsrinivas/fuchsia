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

// How to search for the service using Hub:
static char kGlobStr[] =
    "/hub/r/session-*/*/c/camera-gym-manual.cmx/*/out/svc/fuchsia.camera.gym.Controller";
static char kGlobStrNoSession[] =
    "/hub/c/camera-gym-manual.cmx/*/out/svc/fuchsia.camera.gym.Controller";
static char kRegexStr[] = "/camera-gym-manual.cmx/(\\d+)";

std::vector<Service> FindCameraGyms();

ControllerClientApp::ControllerClientApp(/* const char* name, */ async::Loop& loop)
    : loop_(loop), context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

void ControllerClientApp::Start(std::vector<fuchsia::camera::gym::Command> commands) {
  ConnectToServer();
  SendCommand(std::move(commands), 0);
}

bool ControllerClientApp::ConnectToServer() {
  auto gyms = FindCameraGyms();
  FX_LOGS(DEBUG) << "Found " << gyms.size() << " camera gyms";
  for (const auto& gym : gyms) {
    FX_LOGS(DEBUG) << " Gym: " << gym.name << " -- " << gym.service_path;
  }
  if (gyms.size() == 0) {
    fprintf(stderr, "ERROR: No test server instances found\n");
    return false;
  }
  if (gyms.size() > 1) {
    fprintf(stderr, "ERROR: Multiple test server instances found\n");
    return false;
  }
  auto request = controller_.NewRequest().TakeChannel();
  const auto& service_path = gyms[0].service_path;
  zx_status_t status = fdio_service_connect(service_path.c_str(), request.release());
  if (status != ZX_OK) {
    fprintf(stderr, "ERROR: FDIO service connect failure (status: %s)\n",
            zx_status_get_string(status));
    return false;
  }
  return true;
}

static bool FindServicesForPath(char* glob_str, char* regex_str, std::vector<Service>* services) {
  glob_t glob_buf;
  bool service_exists = glob(glob_str, 0, nullptr, &glob_buf) == 0;
  re2::RE2 name_regex(regex_str);
  if (!service_exists) {
    return false;
  }
  for (size_t i = 0; i < glob_buf.gl_pathc; ++i) {
    Service service;
    service.service_path = glob_buf.gl_pathv[i];
    FX_CHECK(re2::RE2::PartialMatch(service.service_path, name_regex, &service.name))
        << service.service_path;
    services->push_back(std::move(service));
  }
  globfree(&glob_buf);
  return true;
}

std::vector<Service> FindCameraGyms() {
  std::vector<Service> gyms;
  if (!FindServicesForPath(kGlobStr, kRegexStr, &gyms)) {
    if (!FindServicesForPath(kGlobStrNoSession, kRegexStr, &gyms)) {
      fprintf(stderr, "ERROR: Service does not exist\n");
    }
  }
  return gyms;
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
