// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_TRACE_SETTINGS_H_
#define GARNET_LIB_TRACE_SETTINGS_H_

#include "lib/fxl/command_line.h"

namespace tracing {

// Settings for initializing the tracing subsystem.
struct TraceSettings {
  // The label to present to the user to identify the provider.
  // If empty, uses the name of the current process.
  std::string provider_label;
};

// Parses trace settings from standard command-line options.
//
// Recognizes the following options:
//   --trace-label=<label> : sets |label| to label
//
// Returns false and leaves |out_settings| unchanged if there was an
// error parsing the options.  Otherwise updates |out_settings| with any
// values which were overridden by the command-line.
bool ParseTraceSettings(const fxl::CommandLine& command_line,
                        TraceSettings* out_settings);

}  // namespace tracing

#endif  // GARNET_LIB_TRACE_SETTINGS_H_
