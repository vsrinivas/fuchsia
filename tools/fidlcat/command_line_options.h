// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_COMMAND_LINE_OPTIONS_H_
#define TOOLS_FIDLCAT_COMMAND_LINE_OPTIONS_H_

#include <optional>
#include <string>
#include <vector>

#include "src/lib/fidl_codec/display_options.h"
#include "tools/fidlcat/lib/decode_options.h"

namespace fidlcat {

struct CommandLineOptions {
  // debug agent options:
  std::optional<std::string> connect;
  std::vector<std::string> symbol_index_files;
  std::vector<std::string> build_id_dirs;
  std::vector<std::string> symbol_servers;
  std::vector<std::string> symbol_paths;
  std::optional<std::string> symbol_cache;
  // fidlcat system option:
  std::vector<std::string> fidl_ir_paths;
  std::vector<std::string> ids_txts;
  bool quit_agent_on_exit = false;
  // Input option:
  std::string from;
  // Session save option:
  std::string to;
  // Format (output) option:
  std::optional<std::string> format;
  // Extra generation:
  std::vector<std::string> extra_generation;
  // Session comparison option:
  std::optional<std::string> compare_file;
  // Display options:
  bool with_process_info = false;
  int stack_level = 0;
  std::vector<std::string> syscall_filters;
  std::vector<std::string> exclude_syscall_filters;
  std::vector<std::string> message_filters;
  std::vector<std::string> exclude_message_filters;
  std::vector<std::string> trigger_filters;
  bool dump_messages = false;
  std::string colors = "auto";
  int columns = 0;
  // Logging options:
  std::optional<std::string> verbose;
  std::optional<std::string> quiet;
  std::optional<std::string> log_file;
  // Monitoring options:
  std::vector<std::string> remote_pid;
  std::vector<std::string> remote_name;
  std::vector<std::string> extra_name;
};

// Parses the given |argc| and |argv| into the well-defined |options|.  If there
// are strings left over, they are put in |params|.
std::string ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                             DecodeOptions* decode_options, DisplayOptions* display_options,
                             std::vector<std::string>* params);

// Gets the list of .fidl.json files from the command line flags.
//
// For each element in |cli_ir_paths|, add all transitively reachable .fidl.json
// files, and store them in |paths|.  Paths that are not available in the system
// will be added to |bad_paths|.
void ExpandFidlPathsFromOptions(std::vector<std::string> cli_ir_paths,
                                std::vector<std::string>& paths,
                                std::vector<std::string>& bad_paths);

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_COMMAND_LINE_OPTIONS_H_
