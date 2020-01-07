// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_line_options.h"

#include <lib/cmdline/args_parser.h>

namespace zxdb {

namespace {

// Appears at the top of the --help output above the switch list.
const char kHelpIntro[] = R"(zxdb [ <options> ]

  For information on using the debugger, type "help" at the interactive prompt.

Options

)";

const char kBuildDirsHelp[] = R"(  --build-dirs=<path>
  -b <path>
      Adds the given directory to the list of build directories. These
      directories are where source file names from the symbols are relative to.
      There can be multiple ones which will be searched in order.
      The populates the "build-dirs" setting (see "get build-dirs").)";

const char kConnectHelp[] = R"(  --connect=<host>:<port>
  -c <host>:<port>
      Attempts to connect to a debug_agent running on the given host/port.)";

const char kCoreHelp[] = R"(  --core=<filename>
      Attempts to open a core file for analysis.)";

const char kDebugModeHelp[] = R"(  --debug-mode
  -d
      Output debug information about zxdb.
      Should only be useful for people developing zxdb.)";

const char kHelpHelp[] = R"(  --help
  -h
      Prints all command-line switches.)";

const char kRunHelp[] = R"(  --run=<program>
  -r <program>
      Attempts to run a binary in the target system. The debugger must be
      already connected to the debug_agent (use with -c).)";

const char kFilterHelp[] = R"(  --filter=<regexp>
  -f <regexp>
      Adds a job filter to the default job. This will automatically attach
      to processes matching this regexp that are launched in the job. Multiple
      filters can be specified to match more than one process.)";

const char kQuitAgentOnExit[] = R"(  --quit-agent-on-exit
      Will send a quit message to a connected debug agent in order for it to
      shutdown. This is so that zxdb doesn't leak unwanted debug agents on
      "on-the-fly" debugging sessions.)";

const char kScriptFileHelp[] = R"(  --script-file=<file>
  -S <file>
      Reads a script file from a file. The file must contains valid zxdb
      commands as they would be input from the command line. They will be
      executed sequentially.)";

const char kSymbolCachePathHelp[] = R"(  --symbol-cache=<path>
      Path where we can keep a symbol cache. A folder called <path>/.build-id
      will be created if it does not exist, and symbols will be read from this
      location as though you had specified "-s <path>". If a symbol server has
      been specified, downloaded symbols will be stored in the .build-id
      folder.)";

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

const char kSymbolRepoPathHelp[] = R"(  --symbol-repo-path=<path>
      Adds the given directory to the symbol search path. Multiple
      --symbol-repo-path switches can be passed to add multiple locations. the
      path is always assumed to be a directory, unlike with -s, and the
      directory is assumed to contain an index of all ELF files in the same
      style as the .build-id folder as used with the -s option. This is useful
      if your build ID index is not named .build-id)";

const char kSymbolServerHelp[] = R"(  --symbol-server=<url>
      When symbols are missing, attempt to download them from the given URL.
      will be loaded as an ELF file (if possible).)";

}  // namespace

cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("build-dirs", 'b', kBuildDirsHelp, &CommandLineOptions::build_dirs);
  parser.AddSwitch("connect", 'c', kConnectHelp, &CommandLineOptions::connect);
  parser.AddSwitch("core", 0, kCoreHelp, &CommandLineOptions::core);
  parser.AddSwitch("debug-mode", 'd', kDebugModeHelp, &CommandLineOptions::debug_mode);
  parser.AddSwitch("quit-agent-on-exit", 0, kQuitAgentOnExit,
                   &CommandLineOptions::quit_agent_on_quit);
  parser.AddSwitch("run", 'r', kRunHelp, &CommandLineOptions::run);
  parser.AddSwitch("filter", 'f', kFilterHelp, &CommandLineOptions::filter);
  parser.AddSwitch("script-file", 'S', kScriptFileHelp, &CommandLineOptions::script_file);
  parser.AddSwitch("symbol-cache", 0, kSymbolCachePathHelp, &CommandLineOptions::symbol_cache_path);
  parser.AddSwitch("symbol-path", 's', kSymbolPathHelp, &CommandLineOptions::symbol_paths);
  parser.AddSwitch("symbol-repo-path", 0, kSymbolRepoPathHelp,
                   &CommandLineOptions::symbol_repo_paths);
  parser.AddSwitch("symbol-server", 0, kSymbolServerHelp, &CommandLineOptions::symbol_servers);

  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error())
    return status;

  // Handle --help switch since we're the one that knows about the switches.
  if (requested_help)
    return cmdline::Status::Error(kHelpIntro + parser.GetHelp());

  return cmdline::Status::Ok();
}

}  // namespace zxdb
