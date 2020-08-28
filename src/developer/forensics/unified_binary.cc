// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <cstdlib>

#include "src/developer/forensics/crash_reports/main.h"
#include "src/developer/forensics/exceptions/handler/main.h"
#include "src/developer/forensics/exceptions/main.h"
#include "src/developer/forensics/feedback_data/main.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/main.h"
#include "src/developer/forensics/last_reboot/main.h"

int main(int argc, const char** argv) {
  // For components' main executables, argv[0] is the binary path.
  // For sub-processes, argv[0] is the process name.
  FX_CHECK(argc >= 1);

  const auto argv0 = std::string(argv[0]);
  if (argv0 == "/pkg/bin/crash_reports") {
    return ::forensics::crash_reports::main();
  }
  if (argv0 == "/pkg/bin/exceptions") {
    return ::forensics::exceptions::main();
  }
  if (argv0.rfind("handler_") == 0) {
    return ::forensics::exceptions::handler::main();
  }
  if (argv0 == "/pkg/bin/feedback_data") {
    return ::forensics::feedback_data::main();
  }
  if (argv0 == "/pkg/bin/last_reboot") {
    return ::forensics::last_reboot::main();
  }
  if (argv0 == "system_log_recorder") {
    return ::forensics::feedback_data::system_log_recorder::main();
  }

  return EXIT_FAILURE;
}
