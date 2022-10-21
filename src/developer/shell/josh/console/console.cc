// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "src/developer/shell/josh/console/command_line_options.h"
#include "src/developer/shell/josh/console/li.h"
#include "src/developer/shell/josh/lib/runtime.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

namespace shell {

extern "C" const uint8_t qjsc_repl[];
extern "C" const uint32_t qjsc_repl_size;

extern "C" const uint8_t qjsc_repl_init[];
extern "C" const uint32_t qjsc_repl_init_size;

int ConsoleMain(int argc, const char **argv) {
  CommandLineOptions options;
  std::vector<std::string> params;

  cmdline::Status status = ParseCommandLine(argc, argv, &options, &params);
  if (status.has_error()) {
    fprintf(stderr, "%s\n", status.error_message().c_str());
    return 1;
  }

  Runtime rt;
  if (rt.Get() == nullptr) {
    fprintf(stderr, "Cannot allocate JS runtime");
    return 1;
  }

  Context ctx(&rt);
  if (ctx.Get() == nullptr) {
    fprintf(stderr, "Cannot allocate JS context");
    return 1;
  }

  if (!ctx.InitStd()) {
    ctx.DumpError();
    return 1;
  }

  if (!ctx.InitBuiltins(options.fidl_ir_path, options.boot_js_lib_path)) {
    ctx.DumpError();
    return 1;
  }

  JSContext *ctx_ptr = ctx.Get();

  // TODO(jeremymanson): The second and third parameter below let you define properties on the
  // command line, which might be nice at some point.
  js_std_add_helpers(ctx_ptr, 0, nullptr);

  if (!options.command_string && !options.run_script_path) {
    if (!options.line_editor) {
      // Use the qjs repl for the time being.
      js_std_eval_binary(ctx_ptr, qjsc_repl, qjsc_repl_size, 0);
    } else {
      if (li::LiModuleInit(ctx_ptr, "li_internal") == nullptr) {
        ctx.DumpError();
        return 1;
      }
      js_std_eval_binary(ctx_ptr, qjsc_repl_init, qjsc_repl_init_size, 0);
    }
  } else {
    std::string command_string;
    if (options.run_script_path) {
      std::filesystem::path script_path(*options.run_script_path);
      if (!std::filesystem::exists(script_path)) {
        fprintf(stderr, "FATAL: the script %s does not exist!\n", script_path.string().c_str());
        return 1;
      }
      command_string.append("std.loadScript(\"" + script_path.string() + "\");");
    } else {
      command_string = *options.command_string;
    }

    JSValue result = JS_Eval(ctx_ptr, command_string.c_str(), command_string.length(), "batch", 0);
    if (JS_IsException(result)) {
      ctx.DumpError();
      return 1;
    }
  }
  js_std_loop(ctx_ptr);

  return 0;
}

}  // namespace shell
