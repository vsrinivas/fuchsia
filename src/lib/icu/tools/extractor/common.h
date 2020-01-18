// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ICU_TOOLS_EXTRACTOR_COMMON_H_
#define SRC_LIB_ICU_TOOLS_EXTRACTOR_COMMON_H_

#include <optional>
#include <string>

#include "src/lib/fxl/command_line.h"

namespace icu_data_extractor {

// Options
constexpr char kArgIcuDataPath[] = "icu-data-file";
constexpr char kArgTzResPath[] = "tz-res-dir";
constexpr char kArgOutputPath[] = "output";

// Gets the value of "--output" from a command line.
std::optional<std::string> GetOutputPath(const fxl::CommandLine& command_line);

// Writes the given string to the output path specified in `sub_command_line`, or to `stdout` if
// absent.
int WriteToOutputFileOrStdOut(const fxl::CommandLine& sub_command_line,
                              const std::string& contents);

}  // namespace icu_data_extractor

#endif  // SRC_LIB_ICU_TOOLS_EXTRACTOR_COMMON_H_
