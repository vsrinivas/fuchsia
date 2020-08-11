// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_LINE_OPTIONS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_LINE_OPTIONS_H_

#include <lib/cmdline/status.h>

#include <optional>
#include <string>
#include <vector>

namespace zxdb {

struct CommandLineOptions {
  std::optional<std::string> connect;
  std::optional<std::string> unix_connect;
  bool debug_mode = false;
  bool quit_agent_on_quit = false;
  std::optional<std::string> core;
  std::optional<std::string> run;
  std::vector<std::string> filter;
  std::optional<std::string> script_file;
  std::optional<std::string> symbol_cache;
  std::vector<std::string> symbol_index_files;
  std::vector<std::string> symbol_paths;
  std::vector<std::string> build_id_dirs;
  std::vector<std::string> ids_txts;
  std::vector<std::string> symbol_servers;
  std::vector<std::string> build_dirs;
};

// Parses the given command line into options and params.
//
// Returns an error if the command-line is badly formed. In addition, --help
// text will be returned as an error.
cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_LINE_OPTIONS_H_
