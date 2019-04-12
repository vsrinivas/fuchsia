// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/component_context.h>

#include <chrono>
#include <memory>
#include <thread>

#include "src/cobalt/bin/system-metrics/memory_stats_fetcher.h"
#include "src/cobalt/bin/system-metrics/metrics_registry.cb.h"
#include "src/cobalt/bin/utils/clock.h"

// A daemon to send system metrics to Cobalt.
//
// Usage:
//
// async::Loop loop(&kAsyncLoopConfigAttachToThread);
// std::unique_ptr<sys::ComponentContext> context(
//     sys::ComponentContext::Create());
// SystemMetricsDaemon daemon(loop.dispatcher(), context.get());
// daemon.StartLogging();
// loop.Run();
class SystemMetricsDaemon {
 public:
  // Constructor
  //
  // |dispatcher|. This is used to schedule future work.
  //
  // |context|. The Cobalt LoggerFactory interface is fetched from this context.
  SystemMetricsDaemon(async_dispatcher_t* dispatcher,
                      sys::ComponentContext* context);

  // Starts asynchronously logging all system metrics.
  void StartLogging();

 private:
  friend class SystemMetricsDaemonTest;

  // This private constructor is intended for use in tests. |context| may
  // be null because InitializeLogger() will not be invoked. Instead,
  // pass a non-null |logger| which may be a local mock that does not use FIDL.
  SystemMetricsDaemon(
      async_dispatcher_t* dispatcher, sys::ComponentContext* context,
      fuchsia::cobalt::Logger_Sync* logger,
      std::unique_ptr<cobalt::SteadyClock> clock,
      std::unique_ptr<cobalt::MemoryStatsFetcher> memory_stats_fetcher);

  void InitializeLogger();

  void InitializeRootResourceHandle();

  // Calls LogUpTimeAndLifeTimeEvents,
  // and then uses the |dispatcher| passed to the constructor to
  // schedule the next round.
  void RepeatedlyLogUpTimeAndLifeTimeEvents();

  // Calls LogMemoryUsage,
  // then uses the |dispatcher| passed to the constructor to schedule
  // the next round.
  void RepeatedlyLogMemoryUsage();

  // Returns the amount of time since SystemMetricsDaemon started.
  std::chrono::seconds GetUpTime();

  // Calls LogFuchsiaUpPing and LogFuchsiaLifetimeEvents.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogUpTimeAndLifeTimeEvents();

  // Logs one or more UpPing events depending on how long the device has been
  // up.
  //
  // |uptime| An estimate of how long since device boot time.
  //
  // First the "Up" event is logged indicating only that the device is up.
  //
  // If the device has been up for at least a minute then "UpOneMinute" is also
  // logged.
  //
  // If the device has been up for at least 10 minutes, then "UpTenMinutes" is
  // also logged. Etc.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogFuchsiaUpPing(std::chrono::seconds uptime);

  // Logs one FuchsiaLifetimeEvent event of type "Boot" the first time it
  // is invoked and does nothing on subsequent invocations.
  //
  // Returns the amount of time before this method needs to be invoked again.
  // Currently returns std::chrono::seconds::max().
  std::chrono::seconds LogFuchsiaLifetimeEvents();

  // Logs several different measurements of system-wide memory usage.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogMemoryUsage();

  // Helper function to call Cobalt logger's LogCobaltEvent to log
  // information in one zx_info_kmem_stats_t stats data point.
  void LogMemoryUsageToCobalt(const zx_info_kmem_stats_t& stats,
                              const std::chrono::seconds& uptime);

  // Helper function to translate uptime in seconds to
  // corresponding cobalt event code.
  fuchsia_system_metrics::FuchsiaMemoryExperimental2MetricDimensionTimeSinceBoot
  GetUpTimeEventCode(const std::chrono::seconds& uptime);

  // TODO(PT-128) To be removed after we start populating
  // fuchsia_memory_experimental_2.
  // Helper function to call Cobalt logger's LogCobaltEvent to log
  // information in one zx_info_kmem_stats_t stats data point.
  void LogMemoryUsageToCobalt(const zx_info_kmem_stats_t& stats);

  bool boot_reported_ = false;
  async_dispatcher_t* const dispatcher_;
  sys::ComponentContext* context_;
  fuchsia::cobalt::LoggerFactorySyncPtr factory_;
  fuchsia::cobalt::LoggerSyncPtr logger_fidl_proxy_;
  fuchsia::cobalt::Logger_Sync* logger_;
  std::chrono::steady_clock::time_point start_time_;
  std::unique_ptr<cobalt::SteadyClock> clock_;
  std::unique_ptr<cobalt::MemoryStatsFetcher> memory_stats_fetcher_;
};

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_
