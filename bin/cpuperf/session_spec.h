// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_SESSION_SPEC_H_
#define GARNET_BIN_CPUPERF_SESSION_SPEC_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include <lib/fxl/time/time_delta.h>
#include <lib/zircon-internal/device/cpu-trace/cpu-perf.h>
#include <lib/zx/time.h>

namespace cpuperf {

// The parameters controlling data collection.

struct SessionSpec {
  static constexpr uint32_t kDefaultBufferSizeInMb = 16u;
  static constexpr fxl::TimeDelta kDefaultDuration{fxl::TimeDelta::FromSeconds(10)};
  static constexpr size_t kDefaultNumIterations = 1u;
  static const char kDefaultOutputPathPrefix[];
  static const char kDefaultSessionResultSpecPath[];

  SessionSpec();

  // Name of the config for reporting and debugging purposes.
  std::string config_name;

  // Configuration for collecting cpu performance data.
  cpuperf_config_t cpuperf_config{};

  // The size of the trace buffer to use, in MB.
  uint32_t buffer_size_in_mb{kDefaultBufferSizeInMb};

  // How long to collect data for.
  fxl::TimeDelta duration{kDefaultDuration};

  // How many iterations of data to collect.
  size_t num_iterations{kDefaultNumIterations};

  // The path prefix of all of the output files.
  std::string output_path_prefix;

  // The path of the session result spec.
  std::string session_result_spec_path;
};

bool DecodeSessionSpec(const std::string& json, SessionSpec* spec);

}  // namespace cpuperf

#endif  // GARNET_BIN_CPUPERF_SESSION_SPEC_H_
