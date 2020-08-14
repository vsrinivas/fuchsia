// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbol-index/command_line_options.h"

#include <lib/cmdline/args_parser.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace symbol_index {

namespace {

const char kHelpIntro[] = R"(symbol-index [ <options> ] <verb> [ <arguments> ... ]

  Manipulates a symbol-index file.

Available verbs:

  list
      Lists all paths in symbol-index.

  add <symbol path> [ <build directory> ]
      Adds a new symbol path to symbol-index. A symbol path could be either a
      a text file in "ids.txt" format, or a directory in ".build-id" structure.
      An optional build directory could be supplemented, which is used by zxdb
      to locate the source code. If the symbol path is already in symbol-index,
      no changes will be made regardless of the optional build directory.

  add-all [ <input file> ]
      Reads the input and adds all symbol paths with optional build directories.
      The input file can contain multiple lines, each describing a symbol path.
      An optional build directory could be supplemented and separated from the
      symbol path with whitespaces. Relative paths will be resolved based on
      the input file. Empty lines and lines starting with "#" will be ignored.
      If the input file is not specified, the input will be read from the stdin.

  remove <symbol path>
      Removes a symbol path from symbol-index.

  purge
      Removes all non-existent paths from symbol-index.

Options

)";

const char kConfigHelp[] = R"(  --config=<path>
  -c <path>
      Path to the symbol-index config file, default to
      ~/.fuchsia/debug/symbol-index.)";

const char kHelpHelp[] = R"(  --help
  -h
      Prints this help.)";
}  // namespace

Error CommandLineOptions::SetVerb(const std::string& str) {
  if (str == "list")
    verb = Verb::kList;
  else if (str == "add")
    verb = Verb::kAdd;
  else if (str == "add-all")
    verb = Verb::kAddAll;
  else if (str == "remove")
    verb = Verb::kRemove;
  else if (str == "purge")
    verb = Verb::kPurge;
  else
    return fxl::StringPrintf("Unsupported verb: %s", str.c_str());
  return "";
}

Error CommandLineOptions::Validate() {
  size_t params_size = params.size();
  switch (verb) {
    case Verb::kList:
      if (params_size != 0)
        return fxl::StringPrintf("Verb list requires 0 arguments, but %lu is given.", params_size);
      break;
    case Verb::kAdd:
      if (params_size < 1 || params_size > 2) {
        return fxl::StringPrintf("Verb add requires 1 or 2 arguments, but %lu is given.",
                                 params_size);
      }
      break;
    case Verb::kAddAll:
      if (params_size > 1) {
        return fxl::StringPrintf("Verb add-all requires 0 or 1 arguments, but %lu is given.",
                                 params_size);
      }
      break;
    case Verb::kRemove:
      if (params_size != 1)
        return fxl::StringPrintf("Verb remove requires 1 argument, but %lu is given.", params_size);
      break;
    case Verb::kPurge:
      if (params_size != 0)
        return fxl::StringPrintf("Verb purge requires 0 arguments, but %lu is given.", params_size);
      break;
  }
  return "";
}

Error ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options) {
  std::vector<std::string> params;
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("config", 'c', kConfigHelp, &CommandLineOptions::symbol_index_file);

  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  auto s = parser.Parse(argc, argv, options, &params);
  if (s.has_error()) {
    return s.error_message();
  }

  if (requested_help || params.empty()) {
    return kHelpIntro + parser.GetHelp();
  }

  if (Error err = options->SetVerb(params[0]); !err.empty()) {
    return err;
  }

  options->params.resize(params.size() - 1);
  std::copy(params.begin() + 1, params.end(), options->params.begin());

  if (Error err = options->Validate(); !err.empty()) {
    return err;
  }

  return "";
}

}  // namespace symbol_index
