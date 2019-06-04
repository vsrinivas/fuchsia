// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_H_
#define SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/svc/cpp/services.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <sstream>
#include <string>

#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"
#include "third_party/cobalt/util/clock.h"

namespace cobalt {
namespace testapp {

constexpr size_t kEventAggregatorBackfillDays = 2;

class CobaltTestApp {
 public:
  CobaltTestApp(bool use_network, bool test_for_prober)
      : context_(sys::ComponentContext::Create()),
        logger_(use_network, &cobalt_controller_),
        test_for_prober_(test_for_prober) {
    clock_.reset(new util::SystemClock);
    if (test_for_prober) {
      FXL_LOG(INFO) << "Running the Cobalt test app in prober mode";
    }
  }

  // Runs all of the tests. Returns true if they all pass.
  bool RunTests();

 private:
  // Starts and connects to the cobalt fidl service using the provided
  // parameters, passing the values of the parameters as command-line flags.
  void Connect(
      uint32_t schedule_interval_seconds, uint32_t min_interval_seconds,
      size_t event_aggregator_backfill_days = kEventAggregatorBackfillDays,
      bool start_event_aggregator_worker = false,
      uint32_t initial_interval_seconds = 0);

  bool DoLocalAggregationTests(const size_t backfill_days);

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::cobalt::ControllerSyncPtr cobalt_controller_;
  CobaltTestAppLogger logger_;
  bool test_for_prober_;
  std::unique_ptr<util::ClockInterface> clock_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltTestApp);
};

}  // namespace testapp
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_H_
