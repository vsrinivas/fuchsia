// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fit/function.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/sys/time/lib/network_time/time_server_config.h"
#include "src/sys/time/network_time_service/service.h"

constexpr char kServerConfigPath[] = "/pkg/data/roughtime-servers.json";

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line, {"time", "network_time_service"})) {
    return 1;
  }

  const std::string config_path =
      command_line.GetOptionValueWithDefault("config", kServerConfigPath);
  FX_LOGS(INFO) << "Opening client config from " << config_path;
  time_server::TimeServerConfig server_config;
  if (!server_config.Parse(std::move(config_path))) {
    FX_LOGS(FATAL) << "Failed to parse client config";
    return 1;
  }
  // Currently this only supports one roughtime server.
  time_server::RoughTimeServer server = server_config.ServerList()[0];

  bool immediate = command_line.HasOption("immediate");

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  network_time_service::TimeServiceImpl svc(
      sys::ComponentContext::CreateAndServeOutgoingDirectory(), std::move(server),
      loop.dispatcher());
  if (immediate) {
    svc.Update(3, fuchsia::deprecatedtimezone::TimeService::UpdateCallback([&loop](auto result) {
                 FX_LOGS(INFO) << "time sync result " << (result ? "succeeded" : "failed");
                 loop.Shutdown();
               }));
  }
  loop.Run();
  return 0;
}
