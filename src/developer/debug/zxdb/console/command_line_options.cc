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

const char kBuildDirHelp[] = R"(  --build-dir=<path>
  -b <path>
      Adds the given directory to the list of build directories. These
      directories are where source file names from the symbols are relative to.
      There can be multiple ones which will be searched in order.
      It populates the "build-dirs" setting (see "get build-dirs").)";

const char kUnixConnectHelp[] = R"(  --unix-connect=<filepath>
  -u <filepath>
      Attempts to connect to a debug_agent through a unix socket.)";

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

const char kSymbolIndexHelp[] = R"(  --symbol-index=<path>
      Populates --ids-txt and --build-id-dir using the given symbol-index file,
      which defaults to ~/.fuchsia/debug/symbol-index. The file should be
      created and maintained by the "symbol-index" host tool.)";

const char kSymbolPathHelp[] = R"(  --symbol-path=<path>
  -s <path>
      Adds the given directory or file to the symbol search path. Multiple
      -s switches can be passed to add multiple locations. When a directory
      path is passed, the directory will be enumerated non-recursively to
      index all ELF files. When a file is passed, it will be loaded as an ELF
      file (if possible).)";

const char kBuildIdDirHelp[] = R"(  --build-id-dir=<path>
      Adds the given directory to the symbol search path. Multiple
      --build-id-dir switches can be passed to add multiple directories.
      The directory must have the same structure as a .build-id directory,
      that is, each symbol file lives at xx/yyyyyyyy.debug where xx is
      the first two characters of the build ID and yyyyyyyy is the rest.
      However, the name of the directory doesn't need to be .build-id.)";

const char kIdsTxtHelp[] = R"(  --ids-txt=<path>
      Adds the given file to the symbol search path. Multiple --ids-txt
      switches can be passed to add multiple files. The file, typically named
      "ids.txt", serves as a mapping from build ID to symbol file path and
      should contain multiple lines in the format of "<build ID> <file path>".)";

const char kSymbolCacheHelp[] = R"(  --symbol-cache=<path>
      Directory where we can keep a symbol cache, which defaults to
      ~/.fuchsia/debug/symbol-cache. If a symbol server has been specified,
      downloaded symbols will be stored in this directory. The directory
      structure will be the same as a .build-id directory, and symbols will
      be read from this location as though you had specified
      "--build-id-dir=<path>".)";

const char kSymbolServerHelp[] = R"(  --symbol-server=<url>
      Adds the given URL to symbol servers. Symbol servers host the debug
      symbols for prebuilt binaries and dynamic libraries.)";

}  // namespace

cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("build-dir", 'b', kBuildDirHelp, &CommandLineOptions::build_dirs);
  parser.AddSwitch("connect", 'c', kConnectHelp, &CommandLineOptions::connect);
  parser.AddSwitch("unix-connect", 'u', kUnixConnectHelp, &CommandLineOptions::unix_connect);
  parser.AddSwitch("core", 0, kCoreHelp, &CommandLineOptions::core);
  parser.AddSwitch("debug-mode", 'd', kDebugModeHelp, &CommandLineOptions::debug_mode);
  parser.AddSwitch("quit-agent-on-exit", 0, kQuitAgentOnExit,
                   &CommandLineOptions::quit_agent_on_quit);
  parser.AddSwitch("run", 'r', kRunHelp, &CommandLineOptions::run);
  parser.AddSwitch("filter", 'f', kFilterHelp, &CommandLineOptions::filter);
  parser.AddSwitch("script-file", 'S', kScriptFileHelp, &CommandLineOptions::script_file);
  parser.AddSwitch("symbol-index", 0, kSymbolIndexHelp, &CommandLineOptions::symbol_index_files);
  parser.AddSwitch("symbol-path", 's', kSymbolPathHelp, &CommandLineOptions::symbol_paths);
  parser.AddSwitch("build-id-dir", 0, kBuildIdDirHelp, &CommandLineOptions::build_id_dirs);
  parser.AddSwitch("ids-txt", 0, kIdsTxtHelp, &CommandLineOptions::ids_txts);
  parser.AddSwitch("symbol-cache", 0, kSymbolCacheHelp, &CommandLineOptions::symbol_cache);
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
