// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_LOGGER_IMPL_H_
#define GARNET_BIN_COBALT_APP_LOGGER_IMPL_H_

#include <stdlib.h>

#include <fuchsia/cobalt/cpp/fidl.h>

#include "garnet/bin/cobalt/app/timer_manager.h"
#include "third_party/cobalt/config/client_config.h"
#include "third_party/cobalt/encoder/client_secret.h"
#include "third_party/cobalt/encoder/encoder.h"
#include "third_party/cobalt/encoder/observation_store_dispatcher.h"
#include "third_party/cobalt/encoder/project_context.h"
#include "third_party/cobalt/encoder/send_retryer.h"
#include "third_party/cobalt/encoder/shipping_dispatcher.h"
#include "third_party/cobalt/encoder/shuffler_client.h"
#include "third_party/cobalt/util/encrypted_message_util.h"

namespace cobalt {

class LoggerImpl : public fuchsia::cobalt::Logger {
 public:
  LoggerImpl(std::unique_ptr<encoder::ProjectContext> project_context,
             encoder::ClientSecret client_secret,
             encoder::ObservationStoreDispatcher* store_dispatcher,
             util::EncryptedMessageMaker* encrypt_to_analyzer,
             encoder::ShippingDispatcher* shipping_dispatcher,
             const encoder::SystemData* system_data,
             TimerManager* timer_manager);

 protected:
  template <class CB>
  void AddEncodedObservation(cobalt::encoder::Encoder::Result* result,
                             CB callback);

  bool GetSinglePartMetricInfo(const fidl::StringPtr& metric_name,
                               uint32_t* metric_id, uint32_t* encoding_id);

  void LogEvent(fidl::StringPtr metric_name, uint32_t event_type_index,
                LogEventCallback callback) override;

  void LogEventCount(fidl::StringPtr metric_name, uint32_t event_type_index,
                     fidl::StringPtr component, int64_t period_duration_micros,
                     uint32_t count, LogEventCountCallback callback) override;

  void LogElapsedTime(fidl::StringPtr metric_name, uint32_t event_type_index,
                      fidl::StringPtr component, int64_t elapsed_micros,
                      LogElapsedTimeCallback callback) override;

  void LogFrameRate(fidl::StringPtr metric_name, uint32_t event_type_index,
                    fidl::StringPtr component, float fps,
                    LogFrameRateCallback callback) override;

  void LogMemoryUsage(fidl::StringPtr metric_name, uint32_t event_type_index,
                      fidl::StringPtr component, int64_t bytes,
                      LogMemoryUsageCallback callback) override;

  void LogString(fidl::StringPtr metric_name, fidl::StringPtr s,
                 LogStringCallback callback) override;

  // Adds an observation from the timer given if both StartTimer and EndTimer
  // have been encountered.
  template <class CB>
  void AddTimerObservationIfReady(std::unique_ptr<TimerVal> timer_val_ptr,
                                  CB callback);

  void StartTimer(fidl::StringPtr metric_name, uint32_t event_type_index,
                  fidl::StringPtr component, fidl::StringPtr timer_id,
                  uint64_t timestamp, uint32_t timeout_s,
                  StartTimerCallback callback) override;

  void EndTimer(fidl::StringPtr timer_id, uint64_t timestamp,
                uint32_t timeout_s, EndTimerCallback callback) override;

  cobalt::encoder::Encoder encoder_;
  encoder::ObservationStoreDispatcher* store_dispatcher_;  // not owned
  util::EncryptedMessageMaker* encrypt_to_analyzer_;       // not owned
  encoder::ShippingDispatcher* shipping_dispatcher_;       // not owned
  TimerManager* timer_manager_;                            // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(LoggerImpl);
};

class LoggerExtImpl : public LoggerImpl, public fuchsia::cobalt::LoggerExt {
 public:
  using LoggerImpl::LoggerImpl;

 private:
  void LogIntHistogram(
      fidl::StringPtr metric_name, uint32_t event_type_index,
      fidl::StringPtr component,
      fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram,
      LogIntHistogramCallback callback) override;

  void LogCustomEvent(
      fidl::StringPtr metric_name,
      fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values,
      LogCustomEventCallback callback) override;
};

class LoggerSimpleImpl : public LoggerImpl,
                         public fuchsia::cobalt::LoggerSimple {
 public:
  using LoggerImpl::LoggerImpl;

 private:
  void LogIntHistogram(fidl::StringPtr metric_name, uint32_t event_type_index,
                       fidl::StringPtr component,
                       fidl::VectorPtr<uint32_t> bucket_indices,
                       fidl::VectorPtr<uint64_t> bucket_counts,
                       LogIntHistogramCallback callback) override;

  void LogCustomEvent(fidl::StringPtr metric_name, fidl::StringPtr json_string,
                      LogCustomEventCallback callback) override;
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_LOGGER_IMPL_H_
