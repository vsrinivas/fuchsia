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
#include "lib/syslog/cpp/logger.h"

BT_DECLARE_FAKE_DRIVER();

int main(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);

  // TODO(armansito): It turns out syslog shouldn't be dynamically linked into
  // drivers. Switch to using printf directly instead of syslog and parse
  // command-line args using FXL (which is OK to link into unit tests).
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
