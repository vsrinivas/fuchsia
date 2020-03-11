// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_SPEC_H_
#define GARNET_BIN_TRACE_SPEC_H_

#include <memory>
#include <string>
#include <vector>

#include <lib/zx/time.h>

#include "garnet/lib/measure/argument_value.h"
#include "garnet/lib/measure/duration.h"
#include "garnet/lib/measure/measurements.h"
#include "garnet/lib/measure/time_between.h"

namespace tracing {

constexpr uint32_t kMinBufferSizeMegabytes = 1;
constexpr uint32_t kMaxBufferSizeMegabytes = 64;

// Individual providers can be tuned with these parameters.
struct ProviderSpec {
  std::string name;
  size_t buffer_size_in_mb;
};

enum class BufferingMode {
  // Tracing stops when the buffer is full.
  kOneshot,
  // A circular buffer.
  kCircular,
  // Double buffering.
  kStreaming,
};

struct BufferingModeSpec {
  const char* name;
  BufferingMode mode;
};

// Tracing specification.
// Every member is a unique_ptr so that we can tell if the object was
// present in the spec file.
struct Spec {
  // Test name (for diagnostic purposes, can be elided).
  std::unique_ptr<std::string> test_name;

  // Url of the application to be run.
  std::unique_ptr<std::string> app;

  // Startup arguments passed to the application.
  std::unique_ptr<std::vector<std::string>> args;

  // Whether to treat "app" as a tool to be spawned or a component.
  std::unique_ptr<bool> spawn;

  // If provided, the application is launched in a nested environment with the
  // given name.
  std::unique_ptr<std::string> environment_name;

  // Tracing categories enabled when tracing the application.
  std::unique_ptr<std::vector<std::string>> categories;

  // The buffering mode to use.
  std::unique_ptr<std::string> buffering_mode;

  // The size of the trace buffer to use, in MB.
  std::unique_ptr<size_t> buffer_size_in_mb;

  // Parameters for individual providers.
  std::unique_ptr<std::vector<ProviderSpec>> provider_specs;

  // Duration of the benchmark.
  std::unique_ptr<zx::duration> duration;

  // Measurements to be performed on the captured traces.
  std::unique_ptr<measure::Measurements> measurements;

  // Test suite name to be used for dashboard upload.
  std::unique_ptr<std::string> test_suite_name;
};

const BufferingModeSpec* LookupBufferingMode(const std::string& name);

bool DecodeSpec(const std::string& json, Spec* spec);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_SPEC_H_
