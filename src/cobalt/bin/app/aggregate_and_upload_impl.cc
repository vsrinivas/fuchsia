// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/aggregate_and_upload_impl.h"

#include <chrono>
#include <cmath>
#include <future>
#include <memory>

#include "fuchsia/cobalt/cpp/fidl.h"
#include "src/lib/util/datetime_util.h"
#include "src/public/lib/status.h"
#include "src/public/lib/status_codes.h"
#include "zircon/system/public/zircon/time.h"
#include "zircon/third_party/ulib/musl/include/unistd.h"

namespace cobalt {

// The base delay that the exponential backoff will use.
const uint32_t kExponentialBackoffMicroseconds = 1000000;  // 1000000 usec = 1 sec

AggregateAndUploadImpl::AggregateAndUploadImpl(
    CobaltServiceInterface* cobalt_service,
    MetricEventLoggerFactoryImpl* metric_event_logger_factory_impl)
    : cobalt_service_(cobalt_service),
      metric_event_logger_factory_impl_(metric_event_logger_factory_impl) {}

void AggregateAndUploadImpl::AggregateAndUploadMetricEvents(
    AggregateAndUploadMetricEventsCallback callback) {
  // Shutdown loggers and background aggregator threads.
  ShutdownLoggersAndBackgroundAggregators();

  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  uint32_t utc_day_index = util::TimePointToDayIndexUtc(now);
  uint32_t exp_backoff_multiplier = 0;
  bool should_retry = false;
  uint32_t retry_attempts = 0;
  do {
    if (should_retry) {
      FX_LOGS(WARNING) << "Aggregated observation generation retry attempt: " << ++retry_attempts
                       << ".";

      if (exp_backoff_multiplier > 0) {
        // As the exp_backoff_multiplier increments for every retry with exponential backoff, the
        // sleep will double.
        // For example, if we retried 5 times with exponential backoff, the delay would look like
        // this 1s...2s...4s...8s...16s.
        usleep(static_cast<uint32_t>(std::pow(2, exp_backoff_multiplier - 1) *
                                     kExponentialBackoffMicroseconds));
      }
    }

    // Aggregate
    cobalt::Status status = cobalt_service_->GenerateAggregatedObservations(utc_day_index);
    if (status.ok()) {
      should_retry = false;
    } else {
      StatusCode error_code = status.error_code();
      FX_LOGS(WARNING) << "Aggregated observation generation failed. Failed with " << error_code
                       << ".";
      switch (error_code) {
        case StatusCode::RESOURCE_EXHAUSTED:
          // The RESOURCE_EXHAUSTED StatusCode means that the Observation store is full and we
          // should retry immediately to avoid data loss. Setting the exp_backoff_multiplier_ to
          // zero will run the retry immediately.
          exp_backoff_multiplier = 0;
          should_retry = true;
          FX_LOGS(WARNING) << "Attempting to retry immediately.";
          break;
        case StatusCode::DATA_LOSS:
        case StatusCode::ABORTED:
        case StatusCode::INTERNAL:
        case StatusCode::UNAVAILABLE:
          // For these StatusCodes we want to retry with exponential backoff.
          exp_backoff_multiplier++;
          should_retry = true;
          FX_LOGS(WARNING) << "Attempting to retry with exponential backoff.";
          break;
        default:
          // For all other StatusCodes never retry and exit the loop.
          should_retry = false;
          FX_LOGS(ERROR) << "Will not retry! Error details: " << status.error_details() << ".";
          break;
      }
    }

    // Upload
    std::promise<bool> send_result;
    cobalt_service_->ShippingRequestSendSoon(
        [&send_result](bool success) mutable { send_result.set_value(success); });

    if (!send_result.get_future().get()) {
      should_retry = true;
      exp_backoff_multiplier++;
      FX_LOGS(ERROR)
          << "There was a failure while sending Observations to Cobalt! Will attempt to retry.";
    }
  } while (should_retry);

  callback();
}

void AggregateAndUploadImpl::ShutdownLoggersAndBackgroundAggregators() {
  // Shutdown logger
  FX_LOGS(INFO) << "Shutting down running loggers.";
  metric_event_logger_factory_impl_->ShutDown();
  FX_LOGS(INFO) << "Running loggers have been shut down.";

  // Shutdown background aggregator threads
  FX_LOGS(INFO) << "Shutting down other background aggregator threads.";
  cobalt_service_->ShutDown();
  FX_LOGS(INFO) << "Other background aggregator threads have been shut down.";
}

}  // namespace cobalt
