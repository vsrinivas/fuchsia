// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/josh/console/command_line_options.h"

#include <lib/cmdline/args_parser.h>

#include <filesystem>

#include "src/developer/shell/josh/lib/runtime.h"

#define DEFAULT_FIDL_IR_PATH "/pkg/data/fidling"
#define DEFAULT_BOOT_JS_LIB_PATH "/pkg/data/lib"
#define DEFAULT_STARTUP_JS_DIR_NAME "startup"

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

const char kStartupJsLibPathHelp[] = R"(  --startup-js-lib-path=<path>
  -s <path>
      Automatically load startup JS scripts in the given path after builtin
      JS files are loaded and before running any script or landing on the
      shell.  Defaults to <boot-js-lib-path>/)" DEFAULT_STARTUP_JS_DIR_NAME R"(.
      The order of the JS files to be loaded is defined by )" DEFAULT_SEQUENCE_JSON_FILENAME R"(
      in the directory. The path of scripts is relative to startup-js-lib-path.
      An example of )" DEFAULT_SEQUENCE_JSON_FILENAME R"(:
        {
          "startup": [
            "module1.js",
            "module2.js",
            "module3.js"
          ]
        })";

const char kFidlIrPathHelp[] = R"(  --fidl-ir-path=<path>
  -f <path>
      Look in the given path for FIDL IR.  Defaults to
      )" DEFAULT_FIDL_IR_PATH R"(, and only takes a single path
      element.  This should be fixed, which requires turning the shell
      into a component.)";

const char kLineEditorHelp[] = R"(  --fuchsia-line-editor
  -l
      Use Fuchsia line_input line editor.)";

const char kBootJsLibPathHelp[] = R"(  --boot-js-lib-path=<path>
  -j <path>
      Automatically load builtin JS files from the given path.  Defaults to
      )" DEFAULT_BOOT_JS_LIB_PATH R"(, and only takes a single path
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
  parser.AddSwitch("startup-js-lib-path", 's', kStartupJsLibPathHelp,
                   &CommandLineOptions::startup_js_lib_path);
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
    options->fidl_ir_path = "/ns" DEFAULT_FIDL_IR_PATH;
    if (!std::filesystem::is_directory(options->fidl_ir_path))
      options->fidl_ir_path = DEFAULT_FIDL_IR_PATH;
  }

  if (options->boot_js_lib_path.empty()) {
    options->boot_js_lib_path = "/ns" DEFAULT_BOOT_JS_LIB_PATH;
    if (!std::filesystem::is_directory(options->boot_js_lib_path))
      options->boot_js_lib_path = DEFAULT_BOOT_JS_LIB_PATH;
  }

  if (options->startup_js_lib_path.empty()) {
    /* By default, try options->boot_js_lib_path/DEFAULT_STARTUP_JS_DIR_NAME */
    std::filesystem::path js_startup_path(options->boot_js_lib_path);
    js_startup_path.append(DEFAULT_STARTUP_JS_DIR_NAME);
    if (std::filesystem::is_directory(js_startup_path))
      options->startup_js_lib_path = js_startup_path.string();
  }

  return cmdline::Status::Ok();
}

}  // namespace shell
