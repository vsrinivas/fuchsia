// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_COMMAND_LINE_OPTIONS_H_
#define TOOLS_SYMBOLIZER_COMMAND_LINE_OPTIONS_H_

#include <optional>
#include <string>
#include <vector>

#include "src/lib/analytics/cpp/core_dev_tools/command_line_options.h"
#include "tools/symbolizer/error.h"

namespace symbolizer {

struct CommandLineOptions {
 private:
  using AnalyticsOption = ::analytics::core_dev_tools::AnalyticsOption;

 public:
  std::vector<std::string> symbol_index_files;
  std::vector<std::string> symbol_paths;
  std::vector<std::string> build_id_dirs;
  std::vector<std::string> ids_txts;
  std::vector<std::string> symbol_servers;
  std::optional<std::string> symbol_cache;
  std::optional<std::string> dumpfile_output;

  bool auth_mode = false;
  bool requested_version = false;

  // Whether to omit the "[[[ELF module..." lines in the output.
  bool omit_module_lines = false;

  // Analytics options
  AnalyticsOption analytics = AnalyticsOption::kUnspecified;
  bool analytics_show = false;
};

// Parses the command line into options.
Error ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options);

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_COMMAND_LINE_OPTIONS_H_
