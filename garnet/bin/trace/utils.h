// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_UTILS_H_
#define GARNET_BIN_TRACE_UTILS_H_

#include <zircon/types.h>

#include <memory>
#include <ostream>
#include <string>

#include "src/lib/fxl/command_line.h"

namespace tracing {

// Result of ParseBooleanOption.
enum class OptionStatus {
  PRESENT,
  NOT_PRESENT,
  ERROR,
};

bool BeginsWith(const std::string_view& str, const std::string_view& prefix,
                std::string_view* rest);

OptionStatus ParseBooleanOption(const fxl::CommandLine& command_line, const char* name,
                                bool* out_value);

std::unique_ptr<std::ostream> OpenOutputStream(const std::string& output_file_name, bool compress);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_UTILS_H_
