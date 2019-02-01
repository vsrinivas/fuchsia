// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_RESULTS_EXPORT_H_
#define GARNET_BIN_TRACE_RESULTS_EXPORT_H_

#include <string>
#include <vector>

#include "garnet/lib/measure/results.h"

namespace tracing {

// Exports the given benchmark results as JSON written under |output_file_path|.
bool ExportResults(const std::string& output_file_path,
                   const std::vector<measure::Result>& results);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_RESULTS_EXPORT_H_
