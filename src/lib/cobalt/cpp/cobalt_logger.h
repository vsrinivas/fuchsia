// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_COBALT_CPP_COBALT_LOGGER_H_
#define SRC_LIB_COBALT_CPP_COBALT_LOGGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

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
  // |event_code| The index of the event type that occurred. The indexed
  //     set of all event types is specified in the metric definition.
  virtual void LogEvent(uint32_t metric_id, uint32_t event_code) = 0;

  // Logs that an event has occurred a given number of times.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type EVENT_COUNT.
  //
  // |event_code| The index of the event type that occurred. The indexed
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
  virtual void LogEventCount(uint32_t metric_id, uint32_t event_code, const std::string& component,
                             zx::duration period_duration, int64_t count) = 0;

  // Logs that an event lasted a given amount of time.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type ELAPSED_TIME.
  //
  // |event_code| The index of the event type that occurred. The indexed
  //     set of all event types is specified in the metric definition.
  //
  // |component| Optionally, a component associated with the event may
  //     also be logged. Any notion of component that makes sense may be
  //     used or use the empty string if there is no natural notion of
  //     component.
  //
  // |elapsed_time| The elapsed time of the event.
  virtual void LogElapsedTime(uint32_t metric_id, uint32_t event_code, const std::string& component,
                              zx::duration elapsed_time) = 0;

  // Logs a measured average frame rate.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type FRAME_RATE.
  //
  // |event_code| The index of the event type that associated with the
  //     frame-rate measurement. The indexed set of all event types is
  //     specified in the metric definition.
  //
  // |component| Optionally, a component associated with the frame-rate
  //     measurement may also be logged. Any notion of component that makes
  //     sense may be used or use the empty string if there is no natural
  //     notion of component.
  //
  // |fps| The average-frame rate in frames-per-second.
  virtual void LogFrameRate(uint32_t metric_id, uint32_t event_code, const std::string& component,
                            float fps) = 0;

  // Logs a measured memory usage.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type MEMORY_USAGE.
  //
  // |event_code| The index of the event type associated with the memory
  //     usage. The indexed set of all event types is specified in the metric
  //     definition.
  //
  // |component| Optionally, a component associated with the memory usage
  //     may also be logged. Any notion of component that makes sense may be
  //     used or use the empty string if there is no natural notion of
  //     component.
  //
  // |bytes| The memory used, in bytes.
  virtual void LogMemoryUsage(uint32_t metric_id, uint32_t event_code, const std::string& component,
                              int64_t bytes) = 0;

  // Logs the fact that a given string was used, in a specific context.
  // The semantics of the context and the string is specified in the
  // Metric definition.
  //
  //  This method is intended to be used in the following situation:
  //  * The string s being logged does not contain PII or passwords.
  //  * The set S of all possible strings that may be logged is large.
  //    If the set S is small consider using LogEvent() instead.
  //  * The ultimate data of interest is the statistical distribution of the
  //    most commonly used strings from S over the population of all Fuchsia
  //    devices.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type STRING_USED.
  //
  // |s| The string to log. This should be a human-readable string of
  //      size no more than 256 bytes.
  virtual void LogString(uint32_t metric_id, const std::string& s) = 0;

  // This method is part of Cobalt's helper service for measuring the time
  // delta between two events that occur in different processes. This starts
  // the timer. A corresponding invocation of EndTimer() with the same
  // |timer_id| ends the timer. After both StartTimer() and EnvdTimer() have
  // been invoked, LogElapsedTime() will be invoked with the difference
  // between the end timestamp and the start timestamp as the value of
  // |duration_microseconds|. It is OK if Cobalt receives the EndTimer()
  // call before the StartTimer() call.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type ELAPSED_TIME.
  //
  // |event_code| The index of the event type to associate with the
  // elapsed time. This is passed to LogElapsedTime()
  //
  // |component| Optionally, a component associated with the event may
  //     also be logged. See the description at LogElapsedTime().
  //
  // |timer_id| The ID of the timer being started. This is an arbitrary
  //     non-empty string provided by the caller and it is the caller's
  //     responsibility to ensure that Cobalt receives a pair of
  //     StartTimer(), EndTimer() calls with this id before the timeout
  //     and without any intervening additional calls to StartTimer()
  //     or EndTimer() using the same id. Once such a pair is received
  //     Cobalt will delete the timer with this ID and after that the
  //     ID may be re-used.
  //
  // |timestamp| The timestamp to set as the start of the timer. The absolute
  // value does not matter, only the
  //     difference between the end and start timestamps will be used.
  //
  // |timeout| The duration Cobalt should wait to receive the
  //     corresponding EndTimer() call with the same |timer_id|. If
  //     Cobalt has already received the corresponding EndTimer() call
  //     before receiving this StartTimer() call then this value is
  //     ignored as the timeout has already been set by the EndTimer()
  //     call. If Cobalt does not receive the corresponding EndTimer()
  //     call before the timeout then the timer will be deleted and
  //     this invocation of StartTimer() will be forgotten. Must be a
  //     positive value less than 300 seconds.
  //
  // |status| Returns OK on success. There are two success cases:
  //     (i) Cobalt does not currently have any timers with the given
  //         timer_id. In that case this call creates a new timer with
  //         the given ID and start timestamp.
  //     (ii) Cobalt currently has a timer with the given timer_id for
  //         which it has received exactly one EndTimer() call and no
  //         StartTimer() calls. In this case Cobalt will delete the
  //         timer and invoke LogElapsedTime() using the difference
  //         between the end timestamp and the start timestamp as the
  //         value of |duration_micors|. It is ok if this value is
  //         negative.
  //     Returns INVALID_ARGUMENTS if |timer_id| is empty or the timeout
  //        is not positive and less than 5 minutes.
  //     Returns FAILED_PRECONDITION if Cobalt currently has a timer
  //        with the given timer_ID and it already has a start
  //        timestamp. In this case Cobalt will delete the timer with
  //        the given |timer_id| and this invocation of StartTimer()
  //        will be forgotten.
  //     Any error returned by LogElapsedTime() may also be returned by this
  //     method.
  virtual void StartTimer(uint32_t metric_id, uint32_t event_code, const std::string& component,
                          const std::string& timer_id, zx::time timestamp,
                          zx::duration timeout) = 0;

  // This method is part of Cobalt's helper service for measuring the time
  // delta between two events that occur in different processes. This ends
  // the timer. A corresponding invocation of StartTimer() with the same
  // |timer_id| starts the timer. After both StartTimer() and EndTimer() have
  // been invoked, LogElapsedTime() will be invoked with the difference
  // between the end timestamp and the start timestamp as the value of
  // |duration_microseconds|. It is OK if Cobalt receives the EndTimer()
  // call before the StartTimer() call.
  //
  // |timer_id| The ID of the timer being ended. This is an arbitrary
  //     non-empty string provided by the caller and it is the caller's
  //     responsibility to ensure that Cobalt receives a pair of
  //     StartTimer(), EndTimer() calls with this id before the timeout
  //     and without any intervening additional calls to StartTimer()
  //     or EndTimer() using the same id. Once such a pair is received
  //     Cobalt will delete the timer with this ID and after that the
  //     ID may be re-used.
  //
  // |timestamp| The timestamp to set as the end of the timer. The absolute
  // value does not matter, only the
  //     difference between the end and start timestamps will be used.
  //
  // |timeout| The duration Cobalt should wait to receive the
  //     corresponding EndTimer() call with the same |timer_id|. If
  //     Cobalt has already received the corresponding EndTimer() call
  //     before receiving this StartTimer() call then this value is
  //     ignored as the timeout has already been set by the EndTimer()
  //     call. If Cobalt does not receive the corresponding EndTimer()
  //     call before the timeout then the timer will be deleted and
  //     this invocation of StartTimer() will be forgotten.
  //
  // |status| Returns OK on success. There are two success cases:
  //     (i) Cobalt does not currently have any timers with the given
  //         timer_id. In that case this call creates a new timer with
  //         the given ID and end timestamp.
  //     (ii) Cobalt currently has a timer with the given timer_id for
  //         which it has received exactly one StartTimer() call and no
  //         EndTimer() calls. In this case Cobalt will delete the
  //         timer and invoke LogElapsedTime() using the difference
  //         between the end timestamp and the start timestamp as the
  //         value of |duration_micors|. It is ok if this value is
  //         negative.
  //     Returns INVALID_ARGUMENTS if |timer_id| is empty or the timeout
  //        is not positive and less than 5 minutes.
  //     Returns FAILED_PRECONDITION if Cobalt currently has a timer
  //        with the given timer_ID and it already has an end
  //        timestamp. In this case Cobalt will delete the timer with
  //        the given |timer_id| and this invocation of EndTimer()
  //        will be forgotten.
  //     Any error returned by LogElapsedTime() may also be returned by this
  //     method.
  virtual void EndTimer(const std::string& timer_id, zx::time timestamp, zx::duration timeout) = 0;

  // Logs a histogram over a set of integer buckets. The meaning of the
  // Metric and the buckets is specified in the Metric definition.
  //
  // This method is intended to be used in situations where the client
  // wishes to aggregate a large number of integer-valued measurements
  // *in-process*, prior to submitting the data to Cobalt.
  // One reason a client may wish to do this is that the measurements occur
  // with very high frequency and it is not practical to make a FIDL call
  // for each individual measurement.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type INT_HISTOGRAM.
  //
  // |event_code| The index of the event type associated with the
  // integer-valued measurement. The indexed set of all event types is
  // specified in the metric definition.
  //
  // |component| Optionally, a component associated with integer-valued
  //     measurements may also be logged. Any notion of component that makes
  //     sense may be used or use the empty string if there is no natural
  //     notion of component.
  //
  // |histogram| The histogram to log. Each HistogramBucket gives the count
  //     for one bucket of the histogram. The definitions of the buckets is
  //     given in the Metric definition.
  virtual void LogIntHistogram(uint32_t metric_id, uint32_t event_code,
                               const std::string& component,
                               std::vector<fuchsia::cobalt::HistogramBucket> histogram) = 0;

  // Logs a custom Event. The semantics of the Metric are specified in the
  // Metric defintion.
  //
  // |metric_id| ID of the metric to use. It must be one of the Metrics
  // from the ProjectProfile used to create this CobaltLogger, and it must be of
  // type CUSTOM.
  //
  // |event_values| The values for the custom Event. There is one value for
  // each dimension of the Metric. The number and types of the values must
  // be consistent with the dimensions declared in the Metric definition.
  virtual void LogCustomEvent(uint32_t metric_id,
                              std::vector<fuchsia::cobalt::CustomEventValue> event_values) = 0;

  // Logs a CobaltEvent. This method offers an alternative API to Cobalt that
  // uses a single method with a variadic parameter instead of the multiple
  // methods defined above. The reason to use this method is that a CobaltEvent
  // allows multiple event codes to be specified whereas the methods above only
  // allow a single event code.
  virtual void LogCobaltEvent(fuchsia::cobalt::CobaltEvent event) = 0;

  // Logs a list of CobaltEvents. This method is equivalent to invoking
  // LogCobaltEvent() multiple times but is more efficient as it requires only
  // a single FIDL call.
  virtual void LogCobaltEvents(std::vector<fuchsia::cobalt::CobaltEvent> event) = 0;
};

// Returns a CobaltLogger initialized with the provided parameters.
//
// |dispatcher| A pointer to an async_dispatcher_t to be used for all
// asynchronous operations.
//
// |services| A shared pointer to the ServiceDirectory that provides access to the
// services received by the component using this CobaltLogger.
//
// |registry_path| The path to the registry file for the Cobalt project
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
//
// Use this version of NewCobaltLogger*() when the version of the Cobalt
// registry that was bundled with the Cobalt service itself may not contain the
// latest versions of the metric and report definitions to be used by the
// returned CobaltLogger. This method allows the caller to provide updated
// versions of those definitions.
std::unique_ptr<CobaltLogger> NewCobaltLogger(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    const std::string& registry_path,
    fuchsia::cobalt::ReleaseStage release_stage = fuchsia::cobalt::ReleaseStage::GA);

// Returns a CobaltLogger initialized with the provided parameters.
//
// |dispatcher| A pointer to an async_dispatcher_t to be used for all
// asynchronous operations.
//
// |services| A shared pointer to the ServiceDirectory that provides access to the
// services received by the component using this CobaltLogger.
//
// |profile| A ProjectProfile that contains (among other data) a VMO containing
// the compiled metric and report definitions to be used by the returned
// CobaltLogger.
//
// Use this version of NewCobaltLogger*() when the version of the Cobalt
// registry that was bundled with the Cobalt service itself may not contain the
// latest versions of the metric and report definitions to be used by the
// returned CobaltLogger. This method allows the caller to provide updated
// versions of those definitions.
std::unique_ptr<CobaltLogger> NewCobaltLogger(async_dispatcher_t* dispatcher,
                                              std::shared_ptr<sys::ServiceDirectory> services,
                                              fuchsia::cobalt::ProjectProfile profile);

// Returns a CobaltLogger initialized with the provided parameters.
//
// |dispatcher| A pointer to an async_dispatcher_t to be used for all
// asynchronous operations.
//
// |services| A shared pointer to the ServiceDirectory that provides access to the
// services received by the component using this CobaltLogger.
//
//
// |project_name| The name of the Cobalt project to be associated with the
// returned CobaltLogger.
//
// |release_stage| Optional specification of the current release stage of the
// project associated with the new Logger. This determines which of the defined
// metrics are permitted to be collected. The default value of GA (Generally
// Available) permits only metrics tagged as GA.
//
// Use this version of NewCobaltLogger*() when the version of the Cobalt
// registry that was bundled with the Cobalt service itself contains the latest
// versions of the metric and report definitions to be used by the returned
// CobaltLogger. The |project_name| should be the name of one of the projects in
// that bundled registry.
// DEPRECATED: use NewCobaltLoggerFromProjectId instead.
std::unique_ptr<CobaltLogger> NewCobaltLoggerFromProjectName(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    std::string project_name,
    fuchsia::cobalt::ReleaseStage release_stage = fuchsia::cobalt::ReleaseStage::GA);

// Returns a CobaltLogger initialized with the provided parameters.
//
// |dispatcher| A pointer to an async_dispatcher_t to be used for all
// asynchronous operations.
//
// |services| A shared pointer to the ServiceDirectory that provides access to the
// services received by the component using this CobaltLogger.
//
// |project_id| The ID of the Cobalt project to be associated with the
// returned CobaltLogger.
//
// Use this version of NewCobaltLogger*() when the version of the Cobalt
// registry that was bundled with the Cobalt service itself contains the latest
// versions of the metric and report definitions to be used by the returned
// CobaltLogger. The |project_id| should be the ID of one of the projects in
// that bundled registry.
std::unique_ptr<CobaltLogger> NewCobaltLoggerFromProjectId(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    uint32_t project_id);

}  // namespace cobalt

#endif  // SRC_LIB_COBALT_CPP_COBALT_LOGGER_H_
