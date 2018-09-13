// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_LOGGER_H
#define GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_LOGGER_H

#include <map>
#include <string>

#include <fuchsia/cobalt/cpp/fidl.h>

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

  // Synchronously invokes LogEvent() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogEventAndSend(uint32_t metric_id, uint32_t index,
                       bool use_request_send_soon);
  // Synchronously invokes LogEventCount() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool LogEventCountAndSend(uint32_t metric_id, uint32_t index,
                            const std::string& component, int64_t count,
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
  // Synchronously invokes LogIntHistogram()
  // |num_observations_per_batch_| times using the given parameters. Then
  // invokes CheckForSuccessfulSend().
  bool LogIntHistogramAndSend(uint32_t metric_id,
                              std::map<uint32_t, uint64_t> histogram_map,
                              bool use_request_send_soon);
  // Synchronously invokes LogCustomEvent() for an event with
  // two string parts, |num_observations_per_batch_| times, using the given
  // parameters. Then invokes CheckForSuccessfulSend().
  bool LogStringPairAndSend(uint32_t metric_id, const std::string& part0,
                            const std::string& val0, const std::string& part1,
                            const std::string& val1,
                            bool use_request_send_soon);

  // If |use_network_| is false this method returns true immediately.
  // Otherwise, uses one of two strategies to cause the Observations that
  // have already been given to the Cobalt Client to be sent to the Shuffler
  // and then checks the status of the send. Returns true just in case the
  // send succeeds.
  //
  // |use_request_send_soon| specifies the strategy. If true then we
  // use the method RequestSendSoon() to ask the Cobalt Client to send the
  // Observations soon and return the status. Otherwise we use the method
  // BlockUntilEmpty() to wait for the CobaltClient to have sent all the
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

#endif  // GARNET_BIN_COBALT_TESTAPP_COBALT_TESTAPP_LOGGER_H
