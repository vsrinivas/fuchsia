// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_H
#define GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_H

#include <memory>
#include <sstream>
#include <string>

#include "garnet/bin/cobalt/testapp/cobalt_testapp_encoder.h"
#include "garnet/bin/cobalt/testapp/tests.h"
#include "lib/component/cpp/startup_context.h"
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

class CobaltTestApp {
 public:
  CobaltTestApp(bool use_network, bool do_environment_test,
                int num_observations_per_batch)
      : do_environment_test_(do_environment_test),
        context_(component::StartupContext::CreateFromStartupInfo()),
        encoder_(use_network, num_observations_per_batch) {}

  // We have multiple testing strategies based on the method we use to
  // connect to the FIDL service and the method we use to determine whether
  // or not all of the sends to the Shuffler succeeded. This is the main
  // test function that invokes all of the strategies.
  bool RunTests();

 private:
  // Starts and connects to the cobalt fidl service using the provided
  // scheduling parameters.
  void Connect(uint32_t schedule_interval_seconds,
               uint32_t min_interval_seconds);

  // Loads the CobaltConfig proto for this project and writes it to a VMO.
  // Returns the VMO and the size of the proto in bytes.
  fuchsia::cobalt::ProjectProfile LoadCobaltConfig();

  // Loads the CobaltConfig proto for this project and writes it to a VMO.
  // Returns the VMO and the size of the proto in bytes.
  fuchsia::cobalt::ProjectProfile2 LoadCobaltConfig2();

  // Tests using the strategy of using the scheduling parameters (9999999, 0)
  // meaning that no scheduled sends will occur and RequestSendSoon() will cause
  // an immediate send so that we are effectively putting the ShippingManager
  // into a manual mode in which sends only occur when explicitly requested.
  // The tests invoke RequestSendSoon() when they want to send.
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

  bool RequestSendSoonTests();

  bool TestLogEvent();
  bool TestLogEventCount();
  bool TestLogElapsedTime();
  bool TestLogFrameRate();
  bool TestLogMemoryUsage();
  bool TestLogString();
  bool TestLogTimer();
  bool TestLogCustomEvent();

  // Synchronously invokes LogEvent() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogEventAndSend(uint32_t metric_id, uint32_t index,
                       bool use_request_send_soon);
  // Synchronously invokes LogEventCount() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogEventCountAndSend(uint32_t metric_id, uint32_t index,
                            const std::string& component, uint32_t count,
                            bool use_request_send_soon);
  // Synchronously invokes LogElapsedTime() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogElapsedTimeAndSend(uint32_t metric_id, uint32_t index,
                             const std::string& component,
                             int64_t elapsed_micros,
                             bool use_request_send_soon);
  // Synchronously invokes LogFrameRate() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogFrameRateAndSend(uint32_t metric_id, const std::string& component,
                           float fps, bool use_request_send_soon);
  // Synchronously invokes LogMemoryUsage() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogMemoryUsageAndSend(uint32_t metric_id, uint32_t index, int64_t bytes,
                             bool use_request_send_soon);
  // Synchronously invokes LogString() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogStringAndSend(uint32_t metric_id, const std::string& val,
                        bool use_request_send_soon);
  bool LogTimerAndSend(uint32_t metric_id, uint32_t start_time,
                       uint32_t end_time, const std::string& timer_id,
                       uint32_t timeout_s, bool use_request_send_soon);
  // Synchronously invokes LogCustomEvent() for an event with
  // two string parts, |num_observations_per_batch_| times, using the given
  // parameters. Then invokes CheckForSuccessfulSend().
  bool LogStringPairAndSend(uint32_t metric_id, const std::string& part0,
                            uint32_t encoding_id0, const std::string& val0,
                            const std::string& part1, uint32_t encoding_id1,
                            const std::string& val1,
                            bool use_request_send_soon);

  bool do_environment_test_;
  std::unique_ptr<component::StartupContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  CobaltTestAppEncoder encoder_;
  fuchsia::cobalt::ControllerSyncPtr cobalt_controller_;

  fuchsia::cobalt::LoggerSyncPtr logger_;
  fuchsia::cobalt::LoggerExtSyncPtr logger_ext_;
  fuchsia::cobalt::LoggerSimpleSyncPtr logger_simple_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltTestApp);
};

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_H
