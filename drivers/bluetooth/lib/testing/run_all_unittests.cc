// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include <ddk/driver.h>
#include <syslog/global.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"

#include "lib/fsl/syslogger/init.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/syslog/cpp/logger.h"

BT_DECLARE_FAKE_DRIVER();

int main(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);

  // Set up log settings for FXL_LOG.
  // TODO(armansito): Remove this once users of fxl/logging.h have been removed
  // from the host library.
  if (!fxl::SetLogSettingsFromCommandLine(cl)) {
    FXL_LOG(ERROR) << "Failed to parse log settings from command-line";
    return EXIT_FAILURE;
  }

  // Set up syslog to print to stdout.
  syslog::LogSettings syslog_settings = {FX_LOG_INFO, STDOUT_FILENO};
  std::string error = fsl::ParseLoggerSettings(cl, &syslog_settings);
  if (syslog::InitLogger(syslog_settings, {"unittest"}) != ZX_OK ||
      !error.empty()) {
    return EXIT_FAILURE;
  }

  // Set all library log messages to use syslog instead ddk logging.
  btlib::common::UseSyslog();

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
