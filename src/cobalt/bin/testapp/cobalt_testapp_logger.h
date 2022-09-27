// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_LOGGER_H_
#define SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>

#include <map>
#include <string>

#include "src/cobalt/bin/utils/error_utils.h"

namespace cobalt::testapp {

inline std::string ResultToString(fpromise::result<void, fuchsia::metrics::Error>&& result) {
  if (result.is_ok()) {
    return "OK";
  }

  return ErrorToString(result.error());
}

enum ExperimentArm { kExperiment = 0, kControl = 1, kNone = 2 };

class CobaltTestAppLogger {
 public:
  CobaltTestAppLogger(bool use_network, fuchsia::cobalt::ControllerSyncPtr* cobalt_controller,
                      fuchsia::diagnostics::ArchiveAccessorSyncPtr* inspect_archive)
      : use_network_(use_network),
        cobalt_controller_(cobalt_controller),
        inspect_archive_(inspect_archive) {}

  // Synchronously invokes LogEvent() using the given parameters.
  bool LogEvent(uint32_t metric_id, uint32_t index);

  // Synchronously invokes LogEventCount() using the given parameters.
  bool LogEventCount(uint32_t metric_id, uint32_t index, const std::string& component,
                     int64_t count);

  // Synchronously invokes LogElapsedTime() using the given parameters.
  bool LogElapsedTime(uint32_t metric_id, uint32_t index, const std::string& component,
                      int64_t elapsed_micros);

  // Synchronously invokes LogFrameRate() using the given parameters.
  bool LogFrameRate(uint32_t metric_id, uint32_t index, const std::string& component, float fps);

  // Synchronously invokes LogMemoryUsage() using the given parameters.
  bool LogMemoryUsage(uint32_t metric_id, uint32_t index, const std::string& component,
                      int64_t bytes);

  // Synchronously invokes StartTimer() and EndTimer() using the given
  // parameters.
  bool LogTimer(uint32_t metric_id, uint32_t start_time, uint32_t end_time,
                const std::string& timer_id, uint32_t timeout_s);

  // Synchronously invokes LogIntHistogram() using the given parameters.
  bool LogIntHistogram(uint32_t metric_id, uint32_t index, const std::string& component,
                       const std::map<uint32_t, uint64_t>& histogram_map);

  // Synchronously invokes LogCobaltEvent() using the given parameters.
  bool LogCobaltEvent(fuchsia::cobalt::CobaltEvent event);

  // Synchronously invokes LogOccurrence() using the given parameters.
  bool LogOccurrence(uint32_t metric_id, std::vector<uint32_t> indices, uint64_t count,
                     ExperimentArm arm = kNone);

  // Synchronously invokes LogInteger() using the given parameters.
  bool LogInteger(uint32_t metric_id, std::vector<uint32_t> indices, int64_t value);

  // Synchronously invokes LogIntegerHistogram() using the given parameters.
  bool LogIntegerHistogram(uint32_t metric_id, std::vector<uint32_t> indices,
                           const std::map<uint32_t, uint64_t>& histogram_map);

  // Synchronously invokes LogString() using the given parameters.
  bool LogString(uint32_t metric_id, std::vector<uint32_t> indices,
                 const std::string& string_value);

  // If |use_network_| is false this method returns true immediately.
  //
  // Otherwise, triggers the Cobalt FIDL service to send observations for
  // all of the events that have already been logged. Returns true just in case
  // the send succeeds.
  //
  // We use the method RequestSendSoon() to ask the Cobalt FIDL Service to send
  // the Observations soon and return the status.
  bool CheckForSuccessfulSend();

  // Set the component moniker used by the current Cobalt instance that is being tested.
  void SetCobaltUnderTestMoniker(const std::string& cobalt_under_test_moniker) {
    cobalt_under_test_moniker_ = cobalt_under_test_moniker;
  }

  // Get the inspect JSON for the current Cobalt instance that is being tested.
  std::string GetInspectJson() const;

  bool use_network_;

  fuchsia::cobalt::ControllerSyncPtr* cobalt_controller_;

  fuchsia::cobalt::LoggerSyncPtr logger_;
  fuchsia::metrics::MetricEventLoggerSyncPtr metric_event_logger_;
  fuchsia::metrics::MetricEventLoggerSyncPtr control_metric_event_logger_;
  fuchsia::metrics::MetricEventLoggerSyncPtr experimental_metric_event_logger_;

 private:
  fuchsia::diagnostics::ArchiveAccessorSyncPtr* inspect_archive_;
  std::string cobalt_under_test_moniker_;
};

}  // namespace cobalt::testapp

#endif  // SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_LOGGER_H_
