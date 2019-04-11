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

enum CobaltConfigType {
  kLegacyCobaltConfig = 0,
  kCobaltConfig = 1,
};

class CobaltTestApp {
 public:
  CobaltTestApp(bool use_network, bool do_environment_test,
                int num_observations_per_batch)
      : do_environment_test_(do_environment_test),
        context_(sys::ComponentContext::Create()),
        logger_(use_network, num_observations_per_batch, &cobalt_controller_) {
    clock_.reset(new util::SystemClock);
  }

  // We have multiple testing strategies based on the method we use to
  // connect to the FIDL service and the method we use to determine whether
  // or not all of the sends to the Shuffler succeeded. This is the main
  // test function that invokes all of the strategies.
  bool RunTests();

 private:
  // Starts and connects to the cobalt fidl service using the provided
  // scheduling parameters.
  void Connect(
      uint32_t schedule_interval_seconds, uint32_t min_interval_seconds,
      CobaltConfigType type,
      size_t event_aggregator_backfill_days = kEventAggregatorBackfillDays,
      bool start_event_aggregator_worker = false,
      uint32_t initial_interval_seconds = 0);

  // Loads the CobaltConfig proto for this project and writes it to a VMO.
  // Returns the VMO and the size of the proto in bytes.
  fuchsia::cobalt::ProjectProfile LoadCobaltConfig(CobaltConfigType type);

  // Tests using the strategy of using the scheduling parameters (9999999, 0)
  // meaning that no scheduled sends will occur and RequestSendSoon() will
  // cause an immediate send so that we are effectively putting the
  // ShippingManager into a manual mode in which sends only occur when
  // explicitly requested. The tests invoke RequestSendSoon() when they want
  // to send.
  bool RunTestsWithRequestSendSoon();

  // Tests using the strategy of initializing the ShippingManager with the
  // parameters (1, 0) meaning that scheduled sends will occur every second.
  // The test will then not invoke RequestSendSoon() but rather will add
  // some Observations and then invoke BlockUntilEmpty() and wait up to one
  // second for the sends to occur and then use the NumSendAttempts() and
  // FailedSendAttempts() accessors to determine success.
  bool RunTestsWithBlockUntilEmpty();

  // Tests using the instance of the Cobalt service found in the environment.
  // Since we do not construct the service we do not have the opportunity
  // to configure its scheduling parameters. For this reason we do not
  // wait for and verify a send to the Shuffler, we only verify that we
  // can successfully make FIDL calls
  bool RunTestsUsingServiceFromEnvironment();

  bool LegacyRequestSendSoonTests();
  bool RequestSendSoonTests();
  bool RequestSendSoonTestsWithAggregation(const size_t backfill_days);

  bool do_environment_test_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::cobalt::ControllerSyncPtr cobalt_controller_;
  CobaltTestAppLogger logger_;
  std::unique_ptr<util::ClockInterface> clock_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltTestApp);
};

}  // namespace testapp
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_H_
