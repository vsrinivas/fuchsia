// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/cobalt/app/cobalt_app.h"
#include "garnet/bin/cobalt/app/product_hack.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"

// Command-line flags

// Used to override kScheduleIntervalDefault;
constexpr fxl::StringView kScheduleIntervalSecondsFlagName =
    "schedule_interval_seconds";

// Used to override kMinIntervalDefault;
constexpr fxl::StringView kMinIntervalSecondsFlagName = "min_interval_seconds";

// Because we don't yet persist Observations to local, non-volatile storage,
// we send accumulated Observations every 10 seconds. After persistence is
// implemented this value should be changed to something more like one hour.
const std::chrono::seconds kScheduleIntervalDefault(10);

// We send Observations to the Shuffler more frequently than kScheduleInterval
// under some circumstances, namely, if there is memory pressure or if we
// are explicitly asked to do so via the RequestSendSoon() method. This value
// is a safety parameter. We do not make two attempts within a period of this
// specified length.
const std::chrono::seconds kMinIntervalDefault(1);

int main(int argc, const char** argv) {
  setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH", "/config/ssl/cert.pem", 1);

  // Parse the flags.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  if (fxl::GetVlogVerbosity() >= 10) {
    setenv("GRPC_VERBOSITY", "DEBUG", 1);
    setenv("GRPC_TRACE", "all,-timer,-timer_check", 1);
  }

  // Parse the schedule_interval_seconds flag.
  std::chrono::seconds schedule_interval = kScheduleIntervalDefault;
  std::string flag_value;
  if (command_line.GetOptionValue(kScheduleIntervalSecondsFlagName,
                                  &flag_value)) {
    int num_seconds = std::stoi(flag_value);
    if (num_seconds > 0) {
      schedule_interval = std::chrono::seconds(num_seconds);
    }
  }

  // Parse the min_interval_seconds flag.
  std::chrono::seconds min_interval = kMinIntervalDefault;
  flag_value.clear();
  if (command_line.GetOptionValue(kMinIntervalSecondsFlagName, &flag_value)) {
    int num_seconds = std::stoi(flag_value);
    // We allow min_interval = 0.
    if (num_seconds >= 0) {
      min_interval = std::chrono::seconds(num_seconds);
    }
  }

  FXL_LOG(INFO) << "Cobalt client schedule params: schedule_interval="
                << schedule_interval.count()
                << " seconds, min_interval=" << min_interval.count()
                << " seconds.";

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  cobalt::CobaltApp app(loop.dispatcher(), schedule_interval, min_interval,
                        cobalt::hack::GetLayer());
  loop.Run();
  return 0;
}
