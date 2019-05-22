// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/command_line_options.h"

#include <cmdline/args_parser.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fidlcat {

const char kHelpIntro[] = R"(fidlcat [ <options> ] [ command [args] ]

  fidlcat will run the specified command until it exits.  It will intercept and
  record all fidl calls invoked by the process.  The command may be of the form
  "run <component URL>", in which case the given component will be launched.

Options:

)";

const char kRemoteHostHelp[] = R"(  --connect
      The host and port of the target Fuchsia instance, of the form
      [<ipv6_addr>]:port.)";

const char kRemotePidHelp[] = R"(  --remote-pid
      The koid of the remote process.)";

const char kFidlIrPathHelp[] = R"(  --fidl-ir-path=<path>|@argfile
      Adds the given path as a repository for FIDL IR, in the form of .fidl.json
      files.  Passing a file adds the given file.  Passing a directory adds all
      of the .fidl.json files in that directory and any directory transitively
      reachable from there. An argfile contains a newline-separated list of
      .fidl.json files relative to the directory containing the argfile; passing
      an argfile (starting with the '@' character) adds all files listed in that
      argfile.  This switch can be passed multiple times to add multiple
      locations.)";

const char kSymbolPathHelp[] = R"(  --symbol-path=<path>
  -s <path>
      Adds the given directory or file to the symbol search path. Multiple
      -s switches can be passed to add multiple locations. When a directory
      path is passed, the directory will be enumerated non-recursively to
      index all ELF files, unless the directory contains a .build-id
      subdirectory, in which case that directory is assumed to contain an index
      of all ELF files within. When a .txt file is passed, it will be treated
      as a mapping database from build ID to file path. Otherwise, the path
      will be loaded as an ELF file (if possible).)";

const char kPrettyPrintHelp[] = R"(  --pretty-print
      Use a formated print instead of JSON.)";

const char kColorsHelp[] = R"(  --colors=[never|auto|always]
      For pretty print, use colors:
      - never
      - auto: only if running in a terminal
      - always (default value))";

const char kColumnsHelp[] = R"(  --columns=<size>
      For pretty print, width of the display. By default, on a terminal, use
      the terminal width.)";

const char kHelpHelp[] = R"(  --help
  -h
      Prints all command-line switches.)";

cmdline::Status ParseCommandLine(int argc, const char* argv[],
                                 CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("connect", 'r', kRemoteHostHelp,
                   &CommandLineOptions::connect);
  parser.AddSwitch("remote-pid", 'p', kRemotePidHelp,
                   &CommandLineOptions::remote_pid);
  parser.AddSwitch("fidl-ir-path", 0, kFidlIrPathHelp,
                   &CommandLineOptions::fidl_ir_paths);
  parser.AddSwitch("symbol-path", 's', kSymbolPathHelp,
                   &CommandLineOptions::symbol_paths);
  parser.AddSwitch("pretty-print", 0, kPrettyPrintHelp,
                   &CommandLineOptions::pretty_print);
  parser.AddSwitch("colors", 0, kColorsHelp, &CommandLineOptions::colors);
  parser.AddSwitch("columns", 0, kColumnsHelp, &CommandLineOptions::columns);
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp,
                          [&requested_help]() { requested_help = true; });

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error()) {
    return status;
  }

  if (requested_help) {
    return cmdline::Status::Error(kHelpIntro + parser.GetHelp());
  }

  struct winsize term_size;
  term_size.ws_col = 0;
  int ioctl_result = ioctl(STDOUT_FILENO, TIOCGWINSZ, &term_size);
  if (options->columns == 0) {
    options->columns = term_size.ws_col;
    options->columns = std::max(options->columns, 80);
  }

  if (options->pretty_print) {
    if ((options->colors == "always") ||
        ((options->colors == "auto") && (ioctl_result != -1))) {
      options->needs_colors = true;
    }
  }

  return cmdline::Status::Ok();
}

namespace {

bool EndsWith(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

}  // namespace

void ExpandFidlPathsFromOptions(
    std::vector<std::string> cli_ir_paths,
    std::vector<std::unique_ptr<std::istream>>& paths,
    std::vector<std::string>& bad_paths) {
  // Strip out argfiles before doing path processing.
  for (int i = cli_ir_paths.size() - 1; i >= 0; i--) {
    std::string& path = cli_ir_paths[i];
    if (path.compare(0, 1, "@") == 0) {
      std::filesystem::path real_path(path.substr(1));
      auto enclosing_directory = real_path.parent_path();
      std::string file = path.substr(1);
      cli_ir_paths.erase(cli_ir_paths.begin() + i);

      std::ifstream infile(file, std::ifstream::in);
      if (!infile.good()) {
        bad_paths.push_back(file);
        continue;
      }

      std::string jsonfile;
      while (infile >> jsonfile) {
        if (std::filesystem::path(jsonfile).is_relative()) {
          jsonfile = enclosing_directory.string() +
                     std::filesystem::path::preferred_separator + jsonfile;
        }

        paths.push_back(std::make_unique<std::ifstream>(jsonfile));
      }
    }
  }

  std::set<std::string> checked_dirs;
  // Repeat until cli_ir_paths is empty:
  //  If it is a directory, add the directory contents to the cli_ir_paths.
  //  If it is a .fidl.json file, add it to |paths|.
  while (!cli_ir_paths.empty()) {
    std::string current_string = cli_ir_paths.back();
    cli_ir_paths.pop_back();
    std::filesystem::path current_path = current_string;
    if (std::filesystem::is_directory(current_path)) {
      for (auto& dir_ent : std::filesystem::directory_iterator(current_path)) {
        std::string ent_name = dir_ent.path().string();
        if (std::filesystem::is_directory(ent_name)) {
          auto found = checked_dirs.find(ent_name);
          if (found == checked_dirs.end()) {
            checked_dirs.insert(ent_name);
            cli_ir_paths.push_back(ent_name);
          }
        } else if (EndsWith(ent_name, ".fidl.json")) {
          paths.push_back(std::make_unique<std::ifstream>(dir_ent.path()));
        }
      }
    } else if (std::filesystem::is_regular_file(current_path) &&
               EndsWith(current_string, ".fidl.json")) {
      paths.push_back(std::make_unique<std::ifstream>(current_string));
    } else {
      bad_paths.push_back(current_string);
    }
  }
}

}  // namespace fidlcat
