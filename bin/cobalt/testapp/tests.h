// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTAPP_TESTS_H
#define GARNET_BIN_COBALT_TESTAPP_TESTS_H

#include <memory>
#include <sstream>
#include <string>

#include "garnet/bin/cobalt/testapp/cobalt_testapp_logger.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"

namespace cobalt {
namespace testapp {

namespace legacy {

bool TestLogEvent(CobaltTestAppLogger* logger_);

bool TestLogEventUsingServiceFromEnvironment(CobaltTestAppLogger* logger_);

bool TestLogEventCount(CobaltTestAppLogger* logger_);

bool TestLogElapsedTime(CobaltTestAppLogger* logger_);

bool TestLogFrameRate(CobaltTestAppLogger* logger_);

bool TestLogMemoryUsage(CobaltTestAppLogger* logger_);

bool TestLogString(CobaltTestAppLogger* logger_);

bool TestLogStringUsingBlockUntilEmpty(CobaltTestAppLogger* logger_);

bool TestLogTimer(CobaltTestAppLogger* logger_);

bool TestLogIntHistogram(CobaltTestAppLogger* logger_);

bool TestLogCustomEvent(CobaltTestAppLogger* logger_);

}  // namespace legacy

bool TestLogEvent(CobaltTestAppLogger* logger_);

bool TestLogCustomEvent(CobaltTestAppLogger* logger_);

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_COBALT_TESTS_H
