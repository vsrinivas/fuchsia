// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>
#include <iostream>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/time_service/fidl/time_service.fidl.h"

static constexpr char kGetOffsetCmd[] = "get_offset_minutes";
static constexpr char kSetTimezoneIdCmd[] = "set_timezone_id";
static constexpr char kGetTimezoneIdCmd[] = "get_timezone_id";

class TzUtil {
 public:
  TzUtil() {
    app::ConnectToEnvironmentService(GetSynchronousProxy(&time_svc_));
  }

  void Run(fxl::CommandLine command_line) {
    if (command_line.HasOption("help")) {
      Usage();
      return;
    }
    if (command_line.HasOption(kSetTimezoneIdCmd)) {
      std::string timezone_id;
      command_line.GetOptionValue(kSetTimezoneIdCmd, &timezone_id);
      if (!timezone_id.empty()) {
        bool status;
        time_svc_->SetTimezone(timezone_id, &status);
        if (status) {
          return;
        }
        std::cerr << "ERROR: Unable to set ID." << std::endl;
        exit(1);
      } else {
        Usage();
      }
      return;
    }
    if (command_line.HasOption(kGetTimezoneIdCmd)) {
      fidl::String timezone_id;
      time_svc_->GetTimezoneId(&timezone_id);
      std::cout << timezone_id << std::endl;
      return;
    }
    if (command_line.HasOption(kGetOffsetCmd)) {
      int32_t offset;
      time_t seconds_since_epoch = time(NULL);
      int64_t now_gmt_ms = seconds_since_epoch * 1000;
      time_svc_->GetTimezoneOffsetMinutes(now_gmt_ms, &offset);
      std::cout << offset << std::endl;
      return;
    }

    // Default: no args.
    Usage();
  }

 private:
  static void Usage() {
    std::cout << "Usage: tz-util [--help|"
              << "--" << kSetTimezoneIdCmd << "=ID|"
              << "--" << kGetTimezoneIdCmd << "|"
              << "--" << kGetOffsetCmd << "]" << std::endl;
    std::cout << std::endl;
  }

  time_service::TimeServiceSyncPtr time_svc_;
};

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }
  TzUtil app;
  app.Run(command_line);
  return 0;
}
