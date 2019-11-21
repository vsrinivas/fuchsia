// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/shell/console/command_line_options.h"

#include <cmdline/args_parser.h>

namespace shell {

const char kCommandStringHelp[] = R"(  --command-string=<command-string>
  -c <command string>
      Execute the given command string instead of reading commands
      interactively.)";

const char kFidlIrPathHelp[] = R"(  --fidl-ir-path=<path>
  -f <path>
      Look in the given path for FIDL IR.  Defaults to
      /pkgfs/packages/josh/0/data/fidling, and only takes a single path
      element.  This should be fixed, which requires turning the shell
      into a component.)";
const char kLineEditorHelp[] = R"(  --fuchsia-line-editor
  -l
      Use Fuchsia line_input line editor.)";

const char kBootJsLibPathHelp[] = R"(  --boot-js-lib-path=<path>
  -j <path>
      Automatically load builtin JS files from the given path.  Defaults to
      /pkgfs/packages/josh/0/data/lib, and only takes a single path
      element.  This should be fixed, which requires turning the shell
      into a component.)";

cmdline::Status ParseCommandLine(int argc, const char** argv, CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("command-string", 'c', kCommandStringHelp, &CommandLineOptions::command_string);
  parser.AddSwitch("fidl-ir-path", 'f', kFidlIrPathHelp, &CommandLineOptions::fidl_ir_path);
  parser.AddSwitch("boot-js-lib-path", 'j', kBootJsLibPathHelp,
                   &CommandLineOptions::boot_js_lib_path);
  parser.AddSwitch("fuchsia-line-editor", 'l', kLineEditorHelp, &CommandLineOptions::line_editor);

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error()) {
    return status;
  }

  // TODO(jeremymanson): This is a bad default.
  if (options->fidl_ir_path.empty()) {
    options->fidl_ir_path.push_back("/pkgfs/packages/josh/0/data/fidling");
  }
  if (options->boot_js_lib_path.empty()) {
    options->boot_js_lib_path.push_back("/pkgfs/packages/josh/0/data/lib");
  }

  return cmdline::Status::Ok();
}

}  // namespace shell
