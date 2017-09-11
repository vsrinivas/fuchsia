// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/settings.h"

namespace tracing {

bool ParseTraceSettings(const fxl::CommandLine& command_line,
                        TraceSettings* out_settings) {
  TraceSettings settings = *out_settings;

  // --trace-label=<label>
  size_t index;
  if (command_line.HasOption("trace-label", &index)) {
    settings.provider_label = command_line.options()[index].value;
  }

  *out_settings = settings;
  return true;
}

}  // namespace tracing
