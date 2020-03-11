// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_SESSION_SPEC_H_
#define GARNET_BIN_CPUPERF_SESSION_SPEC_H_

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <lib/zx/time.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "garnet/lib/perfmon/config.h"
#include "garnet/lib/perfmon/events.h"

namespace cpuperf {

// The parameters controlling data collection.

struct SessionSpec {
  static const char kDefaultModelName[];
  static constexpr uint32_t kDefaultBufferSizeInMb = 16u;
  static constexpr zx::duration kDefaultDuration{zx::sec(10)};
  static constexpr size_t kDefaultNumIterations = 1u;
  static const char kDefaultOutputPathPrefix[];
  static const char kDefaultSessionResultSpecPath[];

  SessionSpec();

  // Name of the config for reporting and debugging purposes.
  std::string config_name;

  // The model being used.
  // This affects what performance counters are available.
  // The default is "default" which means use the default for the system
  // we're being run on. But it's useful to be able to modify the default
  // for test purposes.
  std::string model_name;

  // Configuration for collecting cpu performance data.
  perfmon::Config perfmon_config{};

  // The size of the trace buffer to use, in MB.
  uint32_t buffer_size_in_mb{kDefaultBufferSizeInMb};

  // How long to collect data for.
  zx::duration duration{kDefaultDuration};

  // How many iterations of data to collect.
  size_t num_iterations{kDefaultNumIterations};

  // The path prefix of all of the output files.
  std::string output_path_prefix;

  // The path of the session result spec.
  std::string session_result_spec_path;

  // The details of events for |model_name|.
  std::unique_ptr<perfmon::ModelEventManager> model_event_manager;
};

bool DecodeSessionSpec(const std::string& json, SessionSpec* spec);

}  // namespace cpuperf

#endif  // GARNET_BIN_CPUPERF_SESSION_SPEC_H_
