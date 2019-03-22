// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The cobalt system metrics collection daemon uses cobalt to log system metrics
// on a regular basis.

#ifndef GARNET_BIN_COBALT_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_
#define GARNET_BIN_COBALT_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_

#include <chrono>
#include <memory>
#include <thread>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/component_context.h>

#include "src/cobalt/bin/utils/clock.h"

// A daemon to send system metrics to Cobalt.
//
// Usage:
//
// async::Loop loop(&kAsyncLoopConfigAttachToThread);
// std::unique_ptr<sys::ComponentContext> context(
//     sys::ComponentContext::Create());
// SystemMetricsDaemon daemon(loop.dispatcher(), context.get());
// daemon.Work();
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

  // Performs one round of work, depending on the current time relative to when
  // this class was constructed, and then uses the |dispatcher| passed to the
  // constructor to schedule the next round of work.
  void Work();

 private:
  friend class SystemMetricsDaemonTest;

  // This private constructor is intended for use in tests. |context| may
  // be null because InitializeLogger() will not be invoked. Instead,
  // pass a non-null |logger| which may be a local mock that does not use FIDL.
  SystemMetricsDaemon(async_dispatcher_t* dispatcher,
                      sys::ComponentContext* context,
                      fuchsia::cobalt::Logger_Sync* logger,
                      std::unique_ptr<cobalt::SteadyClock> clock);

  void InitializeLogger();

  // Logs one or more events depending on how long the device has been
  // up.
  //
  // Returns the amount of time before this method needs to be invoked again.
  std::chrono::seconds LogMetrics();

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

  bool boot_reported_ = false;
  async_dispatcher_t* const dispatcher_;
  sys::ComponentContext* context_;
  fuchsia::cobalt::LoggerFactorySyncPtr factory_;
  fuchsia::cobalt::LoggerSyncPtr logger_fidl_proxy_;
  fuchsia::cobalt::Logger_Sync* logger_;
  std::chrono::steady_clock::time_point start_time_;
  std::unique_ptr<cobalt::SteadyClock> clock_;
};

#endif  // GARNET_BIN_COBALT_SYSTEM_METRICS_SYSTEM_METRICS_DAEMON_H_
