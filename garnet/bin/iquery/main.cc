// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <unistd.h>

#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>

#include "garnet/bin/iquery/formatter.h"
#include "garnet/bin/iquery/modes.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  iquery::Options options(command_line);
  if (!options.Valid()) {
    return 1;
  }

  if (!options.chdir.empty()) {
    if (chdir(options.chdir.c_str()) != 0) {
      FXL_LOG(ERROR) << "Failed to chdir to " << options.chdir;
    }
  }

  if (command_line.HasOption("help") || options.paths.size() == 0) {
    options.Usage(command_line.argv0());
    return 0;
  }

  std::vector<iquery::ObjectNode> results;
  bool success = false;
  // Dispatch to the correct mode.
  if (options.mode == iquery::Options::Mode::CAT) {
    success = iquery::RunCat(options, &results);
  } else if (options.mode == iquery::Options::Mode::FIND) {
    success = iquery::RunFind(options, &results);
  } else if (options.mode == iquery::Options::Mode::LS) {
    success = iquery::RunLs(options, &results);
  } else {
    FXL_LOG(ERROR) << "Unsupported mode";
    return 1;
  }

  if (!success) {
    FXL_LOG(ERROR) << "Failed running mode. Exiting.";
    return 1;
  }

  // Formatter will handle the correct case according to the options values.
  std::cout << options.formatter->Format(options, results);
  return 0;
}
