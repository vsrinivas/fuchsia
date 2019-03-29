// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTAPP_TESTS_H
#define GARNET_BIN_COBALT_TESTAPP_TESTS_H

#include <memory>
#include <sstream>
#include <string>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"
#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"

namespace cobalt {
namespace testapp {

namespace legacy {

bool TestLogEvent(CobaltTestAppLogger* logger);

bool TestLogEventUsingServiceFromEnvironment(CobaltTestAppLogger* logger);

bool TestLogEventCount(CobaltTestAppLogger* logger);

bool TestLogElapsedTime(CobaltTestAppLogger* logger);

bool TestLogFrameRate(CobaltTestAppLogger* logger);

bool TestLogMemoryUsage(CobaltTestAppLogger* logger);

bool TestLogString(CobaltTestAppLogger* logger);

bool TestLogStringUsingBlockUntilEmpty(CobaltTestAppLogger* logger);

bool TestLogTimer(CobaltTestAppLogger* logger);

bool TestLogIntHistogram(CobaltTestAppLogger* logger);

bool TestLogCustomEvent(CobaltTestAppLogger* logger);

}  // namespace legacy

bool TestLogEvent(CobaltTestAppLogger* logger);

bool TestLogEventCount(CobaltTestAppLogger* logger);

bool TestLogElapsedTime(CobaltTestAppLogger* logger);

bool TestLogFrameRate(CobaltTestAppLogger* logger);

bool TestLogMemoryUsage(CobaltTestAppLogger* logger);

bool TestLogIntHistogram(CobaltTestAppLogger* logger);

bool TestLogCustomEvent(CobaltTestAppLogger* logger);

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_COBALT_TESTS_H
