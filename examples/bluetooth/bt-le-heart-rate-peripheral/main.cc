// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/strings/string_number_conversions.h>

#include "app.h"
#include "system_load_heart_model.h"

int main(int argc, char* argv[]) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  async::Loop message_loop(&kAsyncLoopConfigAttachToThread);

  auto heart_model = std::make_unique<bt_le_heart_rate::SystemLoadHeartModel>();
  bt_le_heart_rate::App app(std::move(heart_model));

  std::string interval_option;
  if (command_line.GetOptionValue("interval", &interval_option)) {
    int measurement_interval;

    if (fxl::StringToNumberWithError(interval_option, &measurement_interval)) {
      app.service()->set_measurement_interval(measurement_interval);
    }
  }

  async::PostTask(message_loop.dispatcher(), [&app] { app.StartAdvertising(); });
  message_loop.Run();

  return EXIT_SUCCESS;
}
