// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_line_options.h"

#include "garnet/bin/zxdb/common/command_line_parser.h"

namespace zxdb {

namespace {

// Appears at the top of the --help output above the switch list.
const char kHelpIntro[] = R"(zxdb [ <options> ]

  For information on using the debugger, type "help" at the interactive prompt.

Options

)";

const char kConnectHelp[] = R"(  --connect=<host>:<port>
  -c <host>:<port>
      Attempts to connect to a debug_agent running on the given host/port.)";

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

const char kQuitAgentOnExit[] = R"(  --quit-agent-on-exit
      Will send a quit message to a connected debug agent in order for it to
      shutdown. This is so that zxdb doesn't leak unwanted debug agents on
      "on-the-fly" debugging sessions.)";

const char kScriptFileHelp[] = R"(  --script-file=<file>
  -S <file>
      Reads a script file from a file. The file must contains valid zxdb
      commands as they would be input from the command line. They will be
      executed sequentially.)";

const char kSymbolPathHelp[] = R"(  --symbol-path=<path>
  -s <path>
      Adds the given directory or file to the symbol search path. Multiple
      -s switches can be passed to add multiple locations. When a directory
      path is passed, the directory will be enumerated non-recursively to
      index all ELF files. When a .txt file is passed, it will be treated
      as a mapping database from build ID to file path. Otherwise, the path
      will be loaded as an ELF file (if possible).)";

const char kSymbolRepoHelp[] = R"(  --symbol-repo=<path>
      Adds the given directory as a symbol repo. Debug symbol files are expected
      to live at <path>/.build-id in a specially organized hierarchy by build
      ID. This switch can be passed multiple times to add multiple locations.)";

}  // namespace

Err ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                     std::vector<std::string>* params) {
  CommandLineParser<CommandLineOptions> parser;

  parser.AddSwitch("connect", 'c', kConnectHelp, &CommandLineOptions::connect);
  parser.AddSwitch("debug-mode", 'd', kDebugModeHelp,
                   &CommandLineOptions::debug_mode);
  parser.AddSwitch("quit-agent-on-exit", 0, kQuitAgentOnExit,
                   &CommandLineOptions::quit_agent_on_quit);
  parser.AddSwitch("run", 'r', kRunHelp, &CommandLineOptions::run);
  parser.AddSwitch("script-file", 'S', kScriptFileHelp,
                   &CommandLineOptions::script_file);
  parser.AddSwitch("symbol-path", 's', kSymbolPathHelp,
                   &CommandLineOptions::symbol_paths);
  parser.AddSwitch("symbol-repo", 0, kSymbolRepoHelp,
                   &CommandLineOptions::symbol_repo_paths);

  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp,
                          [&requested_help]() { requested_help = true; });

  Err err = parser.Parse(argc, argv, options, params);
  if (err.has_error())
    return err;

  // Handle --help switch since we're the one that knows about the switches.
  if (requested_help)
    return Err(kHelpIntro + parser.GetHelp());

  return Err();
}

}  // namespace zxdb
