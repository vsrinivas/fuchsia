// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_PROCESS_LIFECYCLE_IMPL_H_
#define SRC_COBALT_BIN_APP_PROCESS_LIFECYCLE_IMPL_H_

#include <fuchsia/process/lifecycle/cpp/fidl.h>

#include "lib/sys/cpp/outgoing_directory.h"
#include "src/cobalt/bin/app/metric_event_logger_factory_impl.h"
#include "third_party/cobalt/src/public/cobalt_service_interface.h"

namespace cobalt {

class ProcessLifecycle : public fuchsia::process::lifecycle::Lifecycle {
 public:
  ProcessLifecycle(CobaltServiceInterface* cobalt_service,
                   MetricEventLoggerFactoryImpl* metric_event_logger_factory,
                   fit::callback<void()> shutdown,
                   fidl::InterfaceRequest<fuchsia::process::lifecycle::Lifecycle> lifecycle_request,
                   async_dispatcher_t* dispatcher)
      : cobalt_service_(cobalt_service),
        metric_event_logger_factory_(metric_event_logger_factory),
        shutdown_(std::move(shutdown)),
        lifecycle_binding_(this, std::move(lifecycle_request), dispatcher) {}

  // |fuchsia::process::lifecycle::Lifecycle|
  void Stop() override {
    FX_LOGS(INFO) << "Cobalt is initiating shutdown.";
    cobalt_service_->ShutDown();
    metric_event_logger_factory_->ShutDown();
    lifecycle_binding_.Close(ZX_OK);
    shutdown_();
  }

 private:
  CobaltServiceInterface* cobalt_service_;                     // not owned
  MetricEventLoggerFactoryImpl* metric_event_logger_factory_;  // not owned
  fit::callback<void()> shutdown_;
  fidl::Binding<fuchsia::process::lifecycle::Lifecycle> lifecycle_binding_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_PROCESS_LIFECYCLE_IMPL_H_
