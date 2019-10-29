// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <optional>
#include <string>
#include <vector>

#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"
#include "tools/shell/console/command_line_options.h"

namespace shell {

extern "C" const uint8_t qjsc_repl[];
extern "C" const uint32_t qjsc_repl_size;

int ConsoleMain(int argc, const char **argv) {
  CommandLineOptions options;
  std::vector<std::string> params;

  if (!ParseCommandLine(argc, argv, &options, &params).ok()) {
    return 1;
  }

  JSRuntime *rt = JS_NewRuntime();
  if (rt == nullptr) {
    fprintf(stderr, "Cannot allocate JS runtime");
    return 1;
  }

  JSContext *ctx = JS_NewContext(rt);
  if (ctx == nullptr) {
    fprintf(stderr, "Cannot allocate JS context");
    return 1;
  }

  // System modules
  js_init_module_std(ctx, "std");
  js_init_module_os(ctx, "os");

  const char *str =
      "import * as std from 'std';\n"
      "import * as os from 'os';\n"
      "globalThis.std = std;\n"
      "globalThis.os = os;\n";
  JSValue init_compile =
      JS_Eval(ctx, str, strlen(str), "<input>", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

  if (JS_IsException(init_compile)) {
    js_std_dump_error(ctx);
    return 1;
  }
  js_module_set_import_meta(ctx, init_compile, 1, 1);
  JSValue init_run = JS_EvalFunction(ctx, init_compile);

  if (JS_IsException(init_run)) {
    js_std_dump_error(ctx);
    return 1;
  }

  // TODO(jeremymanson): The second and third parameter below let you define properties on the
  // command line, which might be nice at some point.
  js_std_add_helpers(ctx, 0, nullptr);

  if (!options.command_string) {
    // Use the qjs repl for the time being.
    js_std_eval_binary(ctx, qjsc_repl, qjsc_repl_size, 0);
  } else {
    const char *command = options.command_string->c_str();
    JSValue result = JS_Eval(ctx, command, options.command_string->length(), "batch", 0);
    if (JS_IsException(result)) {
      js_std_dump_error(ctx);
      return 1;
    }
  }
  js_std_loop(ctx);

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return 0;
}

}  // namespace shell
