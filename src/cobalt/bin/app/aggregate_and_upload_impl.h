// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_AGGREGATE_AND_UPLOAD_IMPL_H_
#define SRC_COBALT_BIN_APP_AGGREGATE_AND_UPLOAD_IMPL_H_

#include <fuchsia/cobalt/cpp/fidl.h>

#include "src/cobalt/bin/app/metric_event_logger_factory_impl.h"
#include "third_party/cobalt/src/public/cobalt_service_interface.h"

namespace cobalt {

// Implementation of the AggregateAndUpload fidl interface.
class AggregateAndUploadImpl : public fuchsia::cobalt::AggregateAndUpload {
 public:
  // All of the pointers passed to the constructor must be non-null.
  explicit AggregateAndUploadImpl(CobaltServiceInterface* cobalt_service,
                                  MetricEventLoggerFactoryImpl* metric_event_logger_factory);

  // Locally aggregates all collected metrics and uploads generated observations immediately.
  //
  // If AggregateAndUploadMetricEvents completes, then the metrics were locally aggregated and the
  // generated observations were uploaded successfully. Otherwise, AggregateAndUploadMetricEvents
  // will retry until it succeeds, hits a non-retryable error, or the calling service cancels the
  // process.
  void AggregateAndUploadMetricEvents(AggregateAndUploadMetricEventsCallback callback) override;

 private:
  // Shutdown running loggers and other background local aggregators.
  //
  // Shutting down running loggers will prevent any more metrics from being logged when aggregated
  // observations are being generated. Shutting down other background local aggregators will
  // guarantee that metrics will only be locally aggregated when AggregateAndUploadMetricEvents is
  // called.
  void ShutdownLoggersAndBackgroundAggregators();

  CobaltServiceInterface* cobalt_service_;  // not owned
  MetricEventLoggerFactoryImpl* metric_event_logger_factory_impl_;
};
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_AGGREGATE_AND_UPLOAD_IMPL_H_
