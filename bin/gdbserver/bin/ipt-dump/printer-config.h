// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace intel_processor_trace {

// Various kinds of output format.
enum class OutputFormat {
  // Raw format. Prints data for each instruction.
  kRaw,
  // Print data for each function call/return.
  kCalls,
  // Print function call/return data in a format readable by chrome://tracing.
  kChrome
};

// Used by the Chrome output format.
// It only supports processes and threads (major and minor sorting keys).
// We overload that to accommodate cpus.
enum class OutputView {
  // Cpus are on the major key, processes on the minor one.
  kCpu,
  // Processes are on the major key, cpus on the minor one.
  kProcess
};

}  // namespace intel_processor_trace
