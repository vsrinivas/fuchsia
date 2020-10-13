// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/command_line_options.h"

#include <lib/cmdline/args_parser.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace symbolizer {

namespace {

const char kHelpIntro[] = R"(symbolizer [<options>]

  Parses log from stdin and converts symbolizer markups into human readable
  stack traces using local or remote debug symbols.

Options

)";

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

const char kHelpHelp[] = R"(  --help
  -h
      Prints this help.)";

const char kAuthHelp[] = R"(  --auth
      Starts the authentication process for symbol servers.)";

}  // namespace

Error ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options) {
  std::vector<std::string> params;
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("symbol-index", 0, kSymbolIndexHelp, &CommandLineOptions::symbol_index_files);
  parser.AddSwitch("symbol-path", 's', kSymbolPathHelp, &CommandLineOptions::symbol_paths);
  parser.AddSwitch("build-id-dir", 0, kBuildIdDirHelp, &CommandLineOptions::build_id_dirs);
  parser.AddSwitch("ids-txt", 0, kIdsTxtHelp, &CommandLineOptions::ids_txts);
  parser.AddSwitch("symbol-cache", 0, kSymbolCacheHelp, &CommandLineOptions::symbol_cache);
  parser.AddSwitch("symbol-server", 0, kSymbolServerHelp, &CommandLineOptions::symbol_servers);
  parser.AddSwitch("auth", 0, kAuthHelp, &CommandLineOptions::auth_mode);

  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  auto s = parser.Parse(argc, argv, options, &params);
  if (s.has_error()) {
    return s.error_message();
  }

  if (requested_help || !params.empty()) {
    return kHelpIntro + parser.GetHelp();
  }

  // Setup default values.
  if (const char* home = std::getenv("HOME"); home) {
    std::string home_str = home;
    if (!options->symbol_cache) {
      options->symbol_cache = home_str + "/.fuchsia/debug/symbol-cache";
    }
    if (options->symbol_index_files.empty()) {
      options->symbol_index_files.push_back(home_str + "/.fuchsia/debug/symbol-index");
    }
  }

  return Error();
}

}  // namespace symbolizer
