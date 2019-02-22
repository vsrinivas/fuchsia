// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_LEGACY_LOGGER_IMPL_H_
#define GARNET_BIN_COBALT_APP_LEGACY_LOGGER_IMPL_H_

#include <stdlib.h>

#include <fuchsia/cobalt/cpp/fidl.h>

#include "garnet/bin/cobalt/app/timer_manager.h"
#include "third_party/cobalt/encoder/client_secret.h"
#include "third_party/cobalt/encoder/encoder.h"
#include "third_party/cobalt/encoder/observation_store.h"
#include "third_party/cobalt/encoder/project_context.h"
#include "third_party/cobalt/encoder/send_retryer.h"
#include "third_party/cobalt/encoder/shipping_manager.h"
#include "third_party/cobalt/encoder/shuffler_client.h"
#include "third_party/cobalt/util/encrypted_message_util.h"

namespace cobalt {

class LegacyLoggerImpl : public fuchsia::cobalt::Logger,
                         public fuchsia::cobalt::LoggerSimple {
 public:
  LegacyLoggerImpl(std::unique_ptr<encoder::ProjectContext> project_context,
                   encoder::ClientSecret client_secret,
                   encoder::ObservationStore* observation_store,
                   util::EncryptedMessageMaker* encrypt_to_analyzer,
                   encoder::ShippingManager* shipping_manager,
                   const encoder::SystemData* system_data,
                   TimerManager* timer_manager);

 private:
  // Helper function to allow LogEventCount, LogElapsedTime, LogMemoryUsage and
  // LogFrameRate to share their codepaths since they have very similar
  // implementations.
  //
  // If |value_part_required| is true, then |event_code| and |component|
  // are required only if the metric given by |metric_id| has INDEX and STRING
  // parts respectively. If |value_part_required| is false, then at least 2 of
  // |event_code|, |component| and |value| must be supplied and must have
  // corresponding MetricParts. |value_part_name| is only used to identify the
  // metric that could not be logged when an error occurs.
  template <class ValueType, class CB>
  void LogThreePartMetric(const std::string& value_part_name,
                          uint32_t metric_id, uint32_t event_code,
                          fidl::StringPtr component, ValueType value,
                          CB callback, bool value_part_required);

  template <class CB>
  void AddEncodedObservation(cobalt::encoder::Encoder::Result* result,
                             CB callback);

  uint32_t GetSinglePartMetricEncoding(uint32_t metric_id);

  void LogEvent(
      uint32_t metric_id, uint32_t event_code,
      fuchsia::cobalt::LoggerBase::LogEventCallback callback) override;

  // In the current implementation, |period_duration_micros| is ignored
  void LogEventCount(
      uint32_t metric_id, uint32_t event_code, std::string component,
      int64_t period_duration_micros, int64_t count,
      fuchsia::cobalt::LoggerBase::LogEventCountCallback callback) override;

  void LogElapsedTime(
      uint32_t metric_id, uint32_t event_code, std::string component,
      int64_t elapsed_micros,
      fuchsia::cobalt::LoggerBase::LogElapsedTimeCallback callback) override;

  void LogFrameRate(
      uint32_t metric_id, uint32_t event_code, std::string component, float fps,
      fuchsia::cobalt::LoggerBase::LogFrameRateCallback callback) override;

  void LogMemoryUsage(
      uint32_t metric_id, uint32_t event_code, std::string component,
      int64_t bytes,
      fuchsia::cobalt::LoggerBase::LogMemoryUsageCallback callback) override;

  void LogString(
      uint32_t metric_id, std::string s,
      fuchsia::cobalt::LoggerBase::LogStringCallback callback) override;

  // Adds an observation from the timer given if both StartTimer and EndTimer
  // have been encountered.
  template <class CB>
  void AddTimerObservationIfReady(std::unique_ptr<TimerVal> timer_val_ptr,
                                  CB callback);

  void StartTimer(
      uint32_t metric_id, uint32_t event_code, std::string component,
      std::string timer_id, uint64_t timestamp, uint32_t timeout_s,
      fuchsia::cobalt::LoggerBase::StartTimerCallback callback) override;

  void EndTimer(
      std::string timer_id, uint64_t timestamp, uint32_t timeout_s,
      fuchsia::cobalt::LoggerBase::EndTimerCallback callback) override;

  // In the current implementation, |event_code| and |component| are
  // ignored.
  void LogIntHistogram(
      uint32_t metric_id, uint32_t event_code, std::string component,
      std::vector<fuchsia::cobalt::HistogramBucket> histogram,
      fuchsia::cobalt::Logger::LogIntHistogramCallback callback) override;

  void LogIntHistogram(
      uint32_t metric_id, uint32_t event_code, std::string component,
      std::vector<uint32_t> bucket_indices, std::vector<uint64_t> bucket_counts,
      fuchsia::cobalt::LoggerSimple::LogIntHistogramCallback callback) override;

  void LogCustomEvent(
      uint32_t metric_id,
      std::vector<fuchsia::cobalt::CustomEventValue> event_values,
      fuchsia::cobalt::Logger::LogCustomEventCallback callback) override;

  cobalt::encoder::Encoder encoder_;
  encoder::ObservationStore* observation_store_;      // not owned
  util::EncryptedMessageMaker* encrypt_to_analyzer_;  // not owned
  encoder::ShippingManager* shipping_manager_;        // not owned
  TimerManager* timer_manager_;                       // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(LegacyLoggerImpl);
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_LEGACY_LOGGER_IMPL_H_
