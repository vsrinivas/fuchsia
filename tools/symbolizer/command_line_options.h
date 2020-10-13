// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_COMMAND_LINE_OPTIONS_H_
#define TOOLS_SYMBOLIZER_COMMAND_LINE_OPTIONS_H_

#include <optional>
#include <string>
#include <vector>

#include "tools/symbolizer/error.h"

namespace symbolizer {

struct CommandLineOptions {
  std::vector<std::string> symbol_index_files;
  std::vector<std::string> symbol_paths;
  std::vector<std::string> build_id_dirs;
  std::vector<std::string> ids_txts;
  std::vector<std::string> symbol_servers;
  std::optional<std::string> symbol_cache;
  std::vector<std::string> build_dirs;

  bool auth_mode = false;
};

// Parses the command line into options.
Error ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options);

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_COMMAND_LINE_OPTIONS_H_
