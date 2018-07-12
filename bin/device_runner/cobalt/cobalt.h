// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_DEVICE_RUNNER_COBALT_COBALT_H_
#define PERIDOT_BIN_DEVICE_RUNNER_COBALT_COBALT_H_

#include <lib/app/cpp/startup_context.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fxl/functional/auto_call.h>
#include <lib/fxl/memory/ref_ptr.h>

namespace modular {

// Metric IDs that Cobalt requires to identify the data we are logging.
// These are not events (events are tracked through ModularEvents index metric).
// For information on datatypes and structure of each of these metrics, see
// https://cobalt-analytics.googlesource.com/config/+/master/fuchsia/module_usage_tracking/config.yaml
// Next enum value: 6
enum class CobaltMetric : uint32_t {
  MODULE_LAUNCHED = 1,
  MODULE_PAIRS_IN_STORY = 2,
  MODULAR_EVENTS = 3,
  MODULE_LAUNCH_LATENCY = 4,
  STORY_LAUNCH_LATENCY = 5,
};

// The events to report.
// Next enum value: 2
enum class ModularEvent : uint32_t {
  BOOTED_TO_DEVICE_RUNNER = 0,
  BOOTED_TO_USER_RUNNER = 1,
};

// Cobalt initialization. When cobalt is not needed, the returned object must be
// deleted. This method must not be called again until then.
fxl::AutoCall<fit::closure> InitializeCobalt(
    async_dispatcher_t* dispatcher, fuchsia::sys::StartupContext* context);

// Report a modular event to Cobalt.
void ReportEvent(ModularEvent event);

// Report a module launch time duration to Cobalt.
void ReportModuleLaunchTime(std::string module_url, zx_time_t time);

// Report a story launch time duration to Cobalt.
void ReportStoryLaunchTime(zx_time_t time);

}  // namespace modular

#endif  // PERIDOT_BIN_DEVICE_RUNNER_COBALT_COBALT_H_
