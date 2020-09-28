// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_PROCESS_LIFECYCLE_IMPL_H_
#define SRC_COBALT_BIN_APP_PROCESS_LIFECYCLE_IMPL_H_

#include <fuchsia/process/lifecycle/cpp/fidl.h>

#include "fuchsia/cobalt/cpp/fidl.h"
#include "src/cobalt/bin/app/logger_factory_impl.h"
#include "src/cobalt/bin/app/metric_event_logger_factory_impl.h"
#include "third_party/cobalt/src/public/cobalt_service_interface.h"

namespace cobalt {

class ProcessLifecycle : public fuchsia::process::lifecycle::Lifecycle {
 public:
  ProcessLifecycle(CobaltServiceInterface* cobalt_service, LoggerFactoryImpl* logger_factory,
                   MetricEventLoggerFactoryImpl* metric_event_logger_factory)
      : cobalt_service_(cobalt_service),
        logger_factory_(logger_factory),
        metric_event_logger_factory_(metric_event_logger_factory) {}

  // |fuchsia::process::lifecycle::Lifecycle|
  void Stop() override {
    cobalt_service_->ShutDown();
    logger_factory_->ShutDown();
    metric_event_logger_factory_->ShutDown();
  }

 private:
  CobaltServiceInterface* cobalt_service_;                     // not owned
  LoggerFactoryImpl* logger_factory_;                          // not owned
  MetricEventLoggerFactoryImpl* metric_event_logger_factory_;  // not owned
};

}  // namespace cobalt

#endif
