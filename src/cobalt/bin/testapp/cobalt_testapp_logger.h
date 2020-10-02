// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_LOGGER_H_
#define SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>

#include <map>
#include <string>

namespace cobalt::testapp {

class CobaltTestAppLogger {
 public:
  CobaltTestAppLogger(bool use_network, fuchsia::cobalt::ControllerSyncPtr* cobalt_controller)
      : use_network_(use_network), cobalt_controller_(cobalt_controller) {}

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

  // Synchronously invokes LogInteger() using the given parameters.
  bool LogInteger(uint32_t metric_id, std::vector<uint32_t> indices, int64_t value);

  // Synchronously invokes LogCustomEvent() for an event of type
  // cobalt.CobaltMetricsTestProto, using the given parameter values.
  bool LogCustomMetricsTestProto(uint32_t metric_id, const std::string& query_val,
                                 const int64_t wait_time_val, const uint32_t response_code_val);

  // If |use_network_| is false this method returns true immediately.
  //
  // Otherwise, triggers the Cobalt FIDL service to send observations for
  // all of the events that have already been logged. Returns true just in case
  // the send succeeds.
  //
  // We use the method RequestSendSoon() to ask the Cobalt FIDL Service to send
  // the Observations soon and return the status.
  bool CheckForSuccessfulSend();

  bool use_network_;

  fuchsia::cobalt::ControllerSyncPtr* cobalt_controller_;

  fuchsia::cobalt::LoggerSyncPtr logger_;
  fuchsia::cobalt::LoggerSimpleSyncPtr logger_simple_;
  fuchsia::cobalt::MetricEventLoggerSyncPtr metric_event_logger_;
};

}  // namespace cobalt::testapp

#endif  // SRC_COBALT_BIN_TESTAPP_COBALT_TESTAPP_LOGGER_H_
