// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"

#include "src/lib/fxl/logging.h"

namespace cobalt {
namespace testapp {

using fuchsia::cobalt::Status;

bool CobaltTestAppLogger::LogEvent(uint32_t metric_id, uint32_t index) {
  Status status = Status::INTERNAL_ERROR;
  logger_->LogEvent(metric_id, index, &status);
  FXL_VLOG(1) << "LogEvent(" << index << ") => " << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogEvent() => " << StatusToString(status);
    return false;
  }
  return true;
}

bool CobaltTestAppLogger::LogEventAndSend(uint32_t metric_id, uint32_t index,
                                          bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogEvent(metric_id, index)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogEventCount(uint32_t metric_id, uint32_t index,
                                        const std::string& component,
                                        int64_t count) {
  Status status = Status::INTERNAL_ERROR;
  logger_->LogEventCount(metric_id, index, component, 0, count, &status);
  FXL_VLOG(1) << "LogEventCount(" << index << ") => " << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogEventCount() => " << StatusToString(status);
    return false;
  }
  return true;
}

bool CobaltTestAppLogger::LogEventCountAndSend(uint32_t metric_id,
                                               uint32_t index,
                                               const std::string& component,
                                               int64_t count,
                                               bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogEventCount(metric_id, index, component, count)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogElapsedTime(uint32_t metric_id, uint32_t index,
                                         const std::string& component,
                                         int64_t elapsed_micros) {
  Status status = Status::INTERNAL_ERROR;
  logger_->LogElapsedTime(metric_id, index, component, elapsed_micros, &status);
  FXL_VLOG(1) << "LogElapsedTime() => " << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogElapsedTime() => " << StatusToString(status);
    return false;
  }
  return true;
}

bool CobaltTestAppLogger::LogElapsedTimeAndSend(uint32_t metric_id,
                                                uint32_t index,
                                                const std::string& component,
                                                int64_t elapsed_micros,
                                                bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogElapsedTime(metric_id, index, component, elapsed_micros)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogFrameRate(uint32_t metric_id, uint32_t index,
                                       const std::string& component,
                                       float fps) {
  Status status = Status::INTERNAL_ERROR;
  logger_->LogFrameRate(metric_id, index, component, fps, &status);
  FXL_VLOG(1) << "LogFrameRate() => " << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogFrameRate() => " << StatusToString(status);
    return false;
  }
  return true;
}

bool CobaltTestAppLogger::LogFrameRateAndSend(uint32_t metric_id,
                                              uint32_t index,
                                              const std::string& component,
                                              float fps,
                                              bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogFrameRate(metric_id, index, component, fps)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogMemoryUsage(uint32_t metric_id, uint32_t index,
                                         const std::string& component,
                                         int64_t bytes) {
  Status status = Status::INTERNAL_ERROR;
  logger_->LogMemoryUsage(metric_id, index, component, bytes, &status);
  FXL_VLOG(1) << "LogMemoryUsage() => " << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogMemoryUsage() => " << StatusToString(status);
    return false;
  }
  return true;
}

bool CobaltTestAppLogger::LogMemoryUsageAndSend(uint32_t metric_id,
                                                uint32_t index,
                                                const std::string& component,
                                                int64_t bytes,
                                                bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogMemoryUsage(metric_id, index, component, bytes)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogString(uint32_t metric_id,
                                    const std::string& val) {
  Status status = Status::INTERNAL_ERROR;
  logger_->LogString(metric_id, val, &status);
  FXL_VLOG(1) << "LogString(" << val << ") => " << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogString() => " << StatusToString(status);
    return false;
  }

  return true;
}

bool CobaltTestAppLogger::LogStringAndSend(uint32_t metric_id,
                                           const std::string& val,
                                           bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogString(metric_id, val)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogTimer(uint32_t metric_id, uint32_t start_time,
                                   uint32_t end_time,
                                   const std::string& timer_id,
                                   uint32_t timeout_s) {
  Status status = Status::INTERNAL_ERROR;
  logger_->StartTimer(metric_id, 0, "", timer_id, start_time, timeout_s,
                      &status);
  logger_->EndTimer(timer_id, end_time, timeout_s, &status);

  FXL_VLOG(1) << "LogTimer("
              << "timer_id:" << timer_id << ", start_time:" << start_time
              << ", end_time:" << end_time << ") => " << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogTimer() => " << StatusToString(status);
    return false;
  }

  return true;
}

bool CobaltTestAppLogger::LogTimerAndSend(uint32_t metric_id,
                                          uint32_t start_time,
                                          uint32_t end_time,
                                          const std::string& timer_id,
                                          uint32_t timeout_s,
                                          bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogTimer(metric_id, start_time, end_time, timer_id, timeout_s)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogIntHistogram(
    uint32_t metric_id, uint32_t index, const std::string& component,
    const std::map<uint32_t, uint64_t>& histogram_map) {
  Status status = Status::INTERNAL_ERROR;
  std::vector<fuchsia::cobalt::HistogramBucket> histogram;
  for (auto it = histogram_map.begin(); histogram_map.end() != it; it++) {
    fuchsia::cobalt::HistogramBucket entry;
    entry.index = it->first;
    entry.count = it->second;
    histogram.push_back(std::move(entry));
  }

  logger_->LogIntHistogram(metric_id, index, component, std::move(histogram),
                           &status);
  FXL_VLOG(1) << "LogIntHistogram() => " << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogIntHistogram() => " << StatusToString(status);
    return false;
  }

  return true;
}

bool CobaltTestAppLogger::LogIntHistogramAndSend(
    uint32_t metric_id, uint32_t index, const std::string& component,
    const std::map<uint32_t, uint64_t>& histogram_map,
    bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogIntHistogram(metric_id, index, component, histogram_map)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogStringPair(uint32_t metric_id,
                                        const std::string& part0,
                                        const std::string& val0,
                                        const std::string& part1,
                                        const std::string& val1) {
  Status status = Status::INTERNAL_ERROR;
  std::vector<fuchsia::cobalt::CustomEventValue> parts(2);
  parts.at(0).dimension_name = part0;
  parts.at(0).value.set_string_value(val0);
  parts.at(1).dimension_name = part1;
  parts.at(1).value.set_string_value(val1);
  logger_->LogCustomEvent(metric_id, std::move(parts), &status);
  FXL_VLOG(1) << "LogCustomEvent(" << val0 << ", " << val1 << ") => "
              << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogCustomEvent() => " << StatusToString(status);
    return false;
  }

  return true;
}

bool CobaltTestAppLogger::LogStringPairAndSend(uint32_t metric_id,
                                               const std::string& part0,
                                               const std::string& val0,
                                               const std::string& part1,
                                               const std::string& val1,
                                               bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogStringPair(metric_id, part0, val0, part1, val1)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::LogCustomMetricsTestProto(
    uint32_t metric_id, const std::string& query_val,
    const int64_t wait_time_val, const uint32_t response_code_val) {
  Status status = Status::INTERNAL_ERROR;
  std::vector<fuchsia::cobalt::CustomEventValue> parts(3);
  parts.at(0).dimension_name = "query";
  parts.at(0).value.set_string_value(query_val);
  parts.at(1).dimension_name = "wait_time_ms";
  parts.at(1).value.set_int_value(wait_time_val);
  parts.at(2).dimension_name = "response_code";
  parts.at(2).value.set_index_value(response_code_val);
  logger_->LogCustomEvent(metric_id, std::move(parts), &status);
  FXL_VLOG(1) << "LogCustomEvent(query=" << query_val
              << ", wait_time_ms=" << wait_time_val
              << ", response_code=" << response_code_val << ") => "
              << StatusToString(status);
  if (status != Status::OK) {
    FXL_LOG(ERROR) << "LogCustomEvent() => " << StatusToString(status);
    return false;
  }

  return true;
}

bool CobaltTestAppLogger::LogCustomMetricsTestProtoAndSend(
    uint32_t metric_id, const std::string& query_val,
    const int64_t wait_time_val, const uint32_t response_code_val,
    bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    if (!LogCustomMetricsTestProto(metric_id, query_val, wait_time_val,
                                   response_code_val)) {
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestAppLogger::CheckForSuccessfulSend(bool use_request_send_soon) {
  if (!use_network_) {
    FXL_LOG(INFO) << "Not using the network because --no_network_for_testing "
                     "was passed.";
    return true;
  }

  if (use_request_send_soon) {
    // Use the request-send-soon strategy to check the result of the send.
    bool send_success = false;
    FXL_VLOG(1) << "Invoking RequestSendSoon() now...";
    (*cobalt_controller_)->RequestSendSoon(&send_success);
    FXL_VLOG(1) << "RequestSendSoon => " << send_success;
    return send_success;
  }

  // Use the block-until-empty strategy to check the result of the send.
  FXL_VLOG(1) << "Invoking BlockUntilEmpty(10)...";
  (*cobalt_controller_)->BlockUntilEmpty(10);
  FXL_VLOG(1) << "BlockUntilEmpty() returned.";

  uint32_t num_send_attempts;
  (*cobalt_controller_)->GetNumSendAttempts(&num_send_attempts);
  uint32_t failed_send_attempts;
  (*cobalt_controller_)->GetFailedSendAttempts(&failed_send_attempts);
  FXL_VLOG(1) << "num_send_attempts=" << num_send_attempts;
  FXL_VLOG(1) << "failed_send_attempts=" << failed_send_attempts;
  uint32_t expected_lower_bound = previous_value_of_num_send_attempts_ + 1;
  previous_value_of_num_send_attempts_ = num_send_attempts;
  if (num_send_attempts < expected_lower_bound) {
    FXL_LOG(ERROR) << "num_send_attempts=" << num_send_attempts
                   << " expected_lower_bound=" << expected_lower_bound;
    return false;
  }
  if (failed_send_attempts != 0) {
    FXL_LOG(ERROR) << "failed_send_attempts=" << failed_send_attempts;
    return false;
  }
  return true;
}

std::string StatusToString(fuchsia::cobalt::Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case Status::EVENT_TOO_BIG:
      return "EVENT_TOO_BIG";
    case Status::BUFFER_FULL:
      return "BUFFER_FULL";
    case Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
}

}  // namespace testapp
}  // namespace cobalt
