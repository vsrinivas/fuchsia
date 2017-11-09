// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/app/cpp/environment_services.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/time_service/fidl/time_service.fidl.h"

static constexpr char kSetOffsetCmd[] = "set_offset_minutes";
static constexpr char kGetOffsetCmd[] = "get_offset_minutes";

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
    if (command_line.HasOption(kSetOffsetCmd)) {
      std::string offset_str;
      command_line.GetOptionValue(kSetOffsetCmd, &offset_str);
      if (!offset_str.empty()) {
        int64_t offset = fxl::StringToNumber<int64_t>(offset_str);
        SetTimezoneOffset(offset);
      } else {
        Usage();
      }
      return;
    }
    if (command_line.HasOption(kGetOffsetCmd)) {
      int64_t offset;
      time_svc_->GetTimezoneOffsetMinutes(&offset);
      std::cout << offset << std::endl;
      return;
    }

    // Default: no args.
    Usage();
  }

 private:
  static void Usage() {
    std::cout << "Usage: tz-util [--help|--" << kSetOffsetCmd << "=M|"
              << "--" << kGetOffsetCmd << "]" << std::endl;
    std::cout << std::endl;
  }

  void SetTimezoneOffset(int64_t offset) {
    time_service::Status status;
    time_svc_->SetTimezoneOffsetMinutes(offset, &status);
    if (status == time_service::Status::OK) {
      return;
    } else {
      std::cerr << "ERROR: Unable to set offset." << std::endl;
      exit(1);
    }
  }

  time_service::TimeServiceSyncPtr time_svc_;
};

int main(int argc, char **argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }
  TzUtil app;
  app.Run(command_line);
  return 0;
}
