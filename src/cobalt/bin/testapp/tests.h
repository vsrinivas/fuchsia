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
#include "lib/svc/cpp/services.h"
#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"
#include "third_party/cobalt/util/clock.h"

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

// Tests of local aggregation.
//
// Each of these tests assumes that the EventAggregator has been updated with
// the ProjectContext of |logger|, but that the EventAggregator's
// AggregatedObservationHistoryStore is empty and that the LocalAggregateStore
// contains no aggregates. One way to ensure this is to reconnect to the Cobalt
// app immediately before running each of these tests.
//
// Each test logs some events for a locally aggregated report, generates
// locally aggregated observations for the current day index in UTC according to
// a system clock, and checks that the expected number of observations were
// generated. Each test then generates locally aggregated observations
// again, for the same day index, and checks that no observations were
// generated.
//
// In addition, TestLogEventWithAggregation attempts to log an event with an
// invalid event code and checks for failure.
bool TestLogEventWithAggregation(
    CobaltTestAppLogger* logger, util::ClockInterface* clock,
    fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
    const size_t backfill_days);

bool TestLogEventCountWithAggregation(
    CobaltTestAppLogger* logger, util::ClockInterface* clock,
    fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
    const size_t backfill_days);

bool TestLogElapsedTimeWithAggregation(
    CobaltTestAppLogger* logger, util::ClockInterface* clock,
    fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
    const size_t backfill_days);

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_COBALT_TESTS_H
