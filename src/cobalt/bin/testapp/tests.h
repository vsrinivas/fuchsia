// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTAPP_TESTS_H_
#define SRC_COBALT_BIN_TESTAPP_TESTS_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <sstream>
#include <string>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/macros.h"
#include "third_party/cobalt/src/public/lib/clock_interfaces.h"

namespace cobalt {
namespace testapp {

// Tests of Cobalt 1.1 metrics, all of which use local aggregation.
//
// Each of these tests assumes that the local_aggregation_1.1 has been updated with
// the ProjectContext of |logger|, but that the LocalAggregation's LocalAggregateStorage
// contains no aggregates. One way to ensure this is to reconnect to the Cobalt
// app immediately before running each of these tests.
//
// Each test logs some events for a locally aggregated report, generates
// locally aggregated observations for the current day index in UTC according to
// a system clock, and checks that the expected number of observations were
// generated. Each test then generates locally aggregated observations
// again, for the same day index, and checks that no observations were
// generated.
bool TestLogInteger(CobaltTestAppLogger* logger, util::SystemClockInterface* clock,
                    fuchsia::cobalt::ControllerSyncPtr* cobalt_controller, size_t backfill_days,
                    uint32_t project_id);

bool TestLogOccurrence(CobaltTestAppLogger* logger, util::SystemClockInterface* clock,
                       fuchsia::cobalt::ControllerSyncPtr* cobalt_controller, size_t backfill_days,
                       uint32_t project_id);

bool TestLogIntegerHistogram(CobaltTestAppLogger* logger, util::SystemClockInterface* clock,
                             fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                             size_t backfill_days, uint32_t project_id);

bool TestLogString(CobaltTestAppLogger* logger, util::SystemClockInterface* clock,
                   fuchsia::cobalt::ControllerSyncPtr* cobalt_controller, size_t backfill_days,
                   uint32_t project_id);

}  // namespace testapp
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_TESTAPP_TESTS_H_
