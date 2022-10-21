// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/josh/console/command_line_options.h"

#include <lib/cmdline/args_parser.h>

#include <filesystem>

namespace shell {

const char* const kHelpIntro = R"(josh [ <options> ]

  josh is a JavaScript shell for Fuchsia.

Options:

)";

const char kCommandStringHelp[] = R"(  --command-string=<command-string>
  -c <command string>
      Execute the given command string instead of reading commands
      interactively.)";

const char kRunScriptPathHelp[] = R"(  --run-script-path=<script path>
  -r <script path>
      Execute the given script instead of reading commands interactively.
      The execution path will be set the same as the script path.)";

const char kFidlIrPathHelp[] = R"(  --fidl-ir-path=<path>
  -f <path>
      Look in the given path for FIDL IR.  Defaults to
      /pkgfs/packages/data/fidling, and only takes a single path
      element.  This should be fixed, which requires turning the shell
      into a component.)";

const char kLineEditorHelp[] = R"(  --fuchsia-line-editor
  -l
      Use Fuchsia line_input line editor.)";

const char kBootJsLibPathHelp[] = R"(  --boot-js-lib-path=<path>
  -j <path>
      Automatically load builtin JS files from the given path.  Defaults to
      /pkgfs/packages/data/lib, and only takes a single path
      element.  This should be fixed, which requires turning the shell
      into a component.)";

const char* const kHelpHelp = R"(  --help
  -h
      Prints all command-line switches.)";

cmdline::Status ParseCommandLine(int argc, const char** argv, CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("command-string", 'c', kCommandStringHelp, &CommandLineOptions::command_string);
  parser.AddSwitch("run-script-path", 'r', kRunScriptPathHelp,
                   &CommandLineOptions::run_script_path);
  parser.AddSwitch("fidl-ir-path", 'f', kFidlIrPathHelp, &CommandLineOptions::fidl_ir_path);
  parser.AddSwitch("boot-js-lib-path", 'j', kBootJsLibPathHelp,
                   &CommandLineOptions::boot_js_lib_path);
  parser.AddSwitch("fuchsia-line-editor", 'l', kLineEditorHelp, &CommandLineOptions::line_editor);

  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error()) {
    return status;
  }

  if (requested_help) {
    return cmdline::Status::Error(kHelpIntro + parser.GetHelp());
  }

  if (options->fidl_ir_path.empty()) {
    options->fidl_ir_path = "/ns/pkg/data/fidling";
    if (!std::filesystem::exists(options->fidl_ir_path))
      options->fidl_ir_path = "/pkg/data/fidling";
  }

  if (options->boot_js_lib_path.empty()) {
    options->boot_js_lib_path = "/ns/pkg/data/lib";
    if (!std::filesystem::exists(options->boot_js_lib_path))
      options->boot_js_lib_path = "/pkg/data/lib";
  }

  return cmdline::Status::Ok();
}

}  // namespace shell
