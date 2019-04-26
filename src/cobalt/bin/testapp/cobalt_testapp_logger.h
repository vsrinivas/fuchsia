// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_LOGGER_H_
#define GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>

#include <map>
#include <string>

namespace cobalt {
namespace testapp {

std::string StatusToString(fuchsia::cobalt::Status status);

class CobaltTestAppLogger {
 public:
  CobaltTestAppLogger(bool use_network, int num_observations_per_batch,
                      fuchsia::cobalt::ControllerSyncPtr* cobalt_controller)
      : use_network_(use_network),
        num_observations_per_batch_(num_observations_per_batch),
        cobalt_controller_(cobalt_controller) {}

  // Synchronously invokes LogEvent() using the given parameters.
  bool LogEvent(uint32_t metric_id, uint32_t index);

  // Synchronously invokes LogEvent() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogEventAndSend(uint32_t metric_id, uint32_t index,
                       bool use_request_send_soon);

  // Synchronously invokes LogEventCount() using the given parameters.
  bool LogEventCount(uint32_t metric_id, uint32_t index,
                     const std::string& component, int64_t count);

  // Synchronously invokes LogEventCount() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogEventCountAndSend(uint32_t metric_id, uint32_t index,
                            const std::string& component, int64_t count,
                            bool use_request_send_soon);

  // Synchronously invokes LogElapsedTime() using the given parameters.
  bool LogElapsedTime(uint32_t metric_id, uint32_t index,
                      const std::string& component, int64_t elapsed_micros);

  // Synchronously invokes LogElapsedTime() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogElapsedTimeAndSend(uint32_t metric_id, uint32_t index,
                             const std::string& component,
                             int64_t elapsed_micros,
                             bool use_request_send_soon);

  // Synchronously invokes LogFrameRate() using the given parameters.
  bool LogFrameRate(uint32_t metric_id, uint32_t index,
                    const std::string& component, float fps);

  // Synchronously invokes LogFrameRate() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogFrameRateAndSend(uint32_t metric_id, uint32_t index,
                           const std::string& component, float fps,
                           bool use_request_send_soon);

  // Synchronously invokes LogMemoryUsage() using the given parameters.
  bool LogMemoryUsage(uint32_t metric_id, uint32_t index,
                      const std::string& component, int64_t bytes);

  // Synchronously invokes LogMemoryUsage() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogMemoryUsageAndSend(uint32_t metric_id, uint32_t index,
                             const std::string& component, int64_t bytes,
                             bool use_request_send_soon);

  // Synchronously invokes LogString() using the given parameters.
  bool LogString(uint32_t metric_id, const std::string& val);

  // Synchronously invokes LogString() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogStringAndSend(uint32_t metric_id, const std::string& val,
                        bool use_request_send_soon);

  // Synchronously invokes StartTimer() and EndTimer() using the given
  // parameters.
  bool LogTimer(uint32_t metric_id, uint32_t start_time, uint32_t end_time,
                const std::string& timer_id, uint32_t timeout_s);

  // Synchronously invokes StartTimer() and EndTimer()
  // |num_observations_per_batch_| times  using the given parameters. Then
  // invokes CheckForSuccessfulSend().
  bool LogTimerAndSend(uint32_t metric_id, uint32_t start_time,
                       uint32_t end_time, const std::string& timer_id,
                       uint32_t timeout_s, bool use_request_send_soon);

  // Synchronously invokes LogIntHistogram() using the given parameters.
  bool LogIntHistogram(uint32_t metric_id, uint32_t index,
                       const std::string& component,
                       const std::map<uint32_t, uint64_t>& histogram_map);

  // Synchronously invokes LogIntHistogram()
  // |num_observations_per_batch_| times using the given parameters. Then
  // invokes CheckForSuccessfulSend().
  bool LogIntHistogramAndSend(uint32_t metric_id, uint32_t index,
                              const std::string& component,
                              const std::map<uint32_t, uint64_t>& histogram_map,
                              bool use_request_send_soon);

  // Synchronously invokes LogCobaltEvent() using the given parameters.
  bool LogCobaltEvent(fuchsia::cobalt::CobaltEvent event);

  // Synchronously invokes LogCobaltEvent()
  // |num_observations_per_batch_| times using the given parameters. Then
  // invokes CheckForSuccessfulSend().
  bool LogCobaltEventAndSend(fuchsia::cobalt::CobaltEvent event,
                             bool use_request_send_soon);

  // Synchronously invokes LogCustomEvent() for an event with
  // two string parts using the given parameters.
  bool LogStringPair(uint32_t metric_id, const std::string& part0,
                     const std::string& val0, const std::string& part1,
                     const std::string& val1);

  // Synchronously invokes LogCustomEvent() for an event with
  // two string parts, |num_observations_per_batch_| times, using the given
  // parameters. Then invokes CheckForSuccessfulSend().
  bool LogStringPairAndSend(uint32_t metric_id, const std::string& part0,
                            const std::string& val0, const std::string& part1,
                            const std::string& val1,
                            bool use_request_send_soon);

  // Synchronously invokes LogCustomEvent() for an event of type
  // cobalt.CobaltMetricsTestProto, using the given parameter values.
  bool LogCustomMetricsTestProto(uint32_t metric_id,
                                 const std::string& query_val,
                                 const int64_t wait_time_val,
                                 const uint32_t response_code_val);

  // Synchronously invokes LogCustomEvent() for an event of type
  // cobalt.CobaltMetricsTestProto |num_observations_per_batch_| times, using
  // the given parameter values. Then invokes CheckForSuccessfulSend().
  bool LogCustomMetricsTestProtoAndSend(uint32_t metric_id,
                                        const std::string& query_val,
                                        const int64_t wait_time_val,
                                        const uint32_t response_code_val,
                                        bool use_request_send_soon);

  // If |use_network_| is false this method returns true immediately.
  // Otherwise, uses one of two strategies to cause the Observations that
  // have already been given to the Cobalt FIDL Service to be sent to the
  // server and then checks the status of the send. Returns true just in case
  // the send succeeds.
  //
  // |use_request_send_soon| specifies the strategy. If true then we
  // use the method RequestSendSoon() to ask the Cobalt FIDL Service to send the
  // Observations soon and return the status. Otherwise we use the method
  // BlockUntilEmpty() to wait for the Cobalt FIDL service to have sent all the
  // Observations it is holding and then we query NumSendAttempts() and
  // FailedSendAttempts().
  bool CheckForSuccessfulSend(bool use_request_send_soon);

  bool use_network_;
  int num_observations_per_batch_;
  int previous_value_of_num_send_attempts_ = 0;

  fuchsia::cobalt::ControllerSyncPtr* cobalt_controller_;

  fuchsia::cobalt::LoggerSyncPtr logger_;
  fuchsia::cobalt::LoggerSimpleSyncPtr logger_simple_;
};

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_LOGGER_H_
