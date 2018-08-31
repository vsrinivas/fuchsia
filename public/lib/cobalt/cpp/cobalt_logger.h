// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_H_
#define GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>

namespace cobalt {

// If this class is used through multiple threads, it is the caller's
// responsibility to ensure that no task posted on the main thread will outlive
// this class.
class CobaltLogger {
 public:
  virtual ~CobaltLogger() = default;

  // Logs the fact that an event has occurred.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type EVENT_OCCURRED.
  //
  // |event_type_index| The index of the event type that occurred. The indexed
  //     set of all event types is specified in the metric definition.
  virtual void LogEvent(uint32_t metric_id, uint32_t event_type_index) = 0;

  // Logs that an event has occurred a given number of times.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type EVENT_COUNT.
  //
  // |event_type_index| The index of the event type that occurred. The indexed
  //     set of all event types is specified in the metric definition.
  //
  // |component| Optionally, a component associated with the event may
  //     also be logged. Any notion of component that makes sense may be
  //     used or use the empty string if there is no natural notion of
  //     component.
  //
  // |period_duration| Optionally, the period of time over which
  //     the |count| events occurred may be logged. If this is not
  //     relevant the value may be set to 0.
  //
  //  |count| The number of times the event occurred. One may choose to
  //      always set this value to 1 and always set
  //     |period_duration| to 0 in order to achieve a semantics
  //     similar to the LogEvent() method, but with a |component|.
  virtual void LogEventCount(uint32_t metric_id, uint32_t event_type_index,
                             const std::string& component,
                             zx::duration period_duration, int64_t count) = 0;

  // Logs that an event lasted a given amount of time.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type ELAPSED_TIME.
  //
  // |event_type_index| The index of the event type that occurred. The indexed
  //     set of all event types is specified in the metric definition.
  //
  // |component| Optionally, a component associated with the event may
  //     also be logged. Any notion of component that makes sense may be
  //     used or use the empty string if there is no natural notion of
  //     component.
  //
  // |elapsed_time| The elapsed time of the event.
  virtual void LogElapsedTime(uint32_t metric_id, uint32_t event_type_index,
                              const std::string& component,
                              zx::duration elapsed_time) = 0;
};

// Returns a CobaltLogger initialized with the provided parameters.
//
// |dispatcher| A pointer to an async_dispatcher_t to be used for all
// asynchronous operations.
//
// |context| A pointer to the StartupContext that provides access to the
// environment of the component using this CobaltLogger.
//
// |config_path| The path to the configuration file for the Cobalt project
// associated with the new Logger. This is a binary file containing the compiled
// definitions of the metrics and reports defined for the project. Usually this
// file is generated via the |cobalt_config| target in your BUILD file and
// included in your package via a |resources| clause in your |package|
// definition.
//
// |release_stage| Optional specification of the current release stage of the
// project associated with the new Logger. This determines which of the defined
// metrics are permitted to be collected. The default value of GA (Generally
// Available) permits only metrics tagged as GA.
std::unique_ptr<CobaltLogger> NewCobaltLogger(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    const std::string& config_path,
    fuchsia::cobalt::ReleaseStage release_stage =
        fuchsia::cobalt::ReleaseStage::GA);

// Returns a CobaltLogger initialized with the provided parameters.
//
// |dispatcher| A pointer to an async_dispatcher_t to be used for all
// asynchronous operations.
//
// |context| A pointer to the StartupContext that provides access to the
// environment of the component using this CobaltLogger.
//
// |profile| The ProjectProfile2 struct that contains the configuration for this
// CobaltLogger.
std::unique_ptr<CobaltLogger> NewCobaltLogger(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    fuchsia::cobalt::ProjectProfile2 profile);

}  // namespace cobalt

#endif  // GARNET_PUBLIC_LIB_COBALT_CPP_COBALT_LOGGER_H_
