// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <optional>
#include <string>
#include <vector>

#include "src/developer/shell/console/command_line_options.h"
#include "src/developer/shell/lib/li.h"
#include "src/developer/shell/lib/runtime.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

namespace shell {

extern "C" const uint8_t qjsc_repl[];
extern "C" const uint32_t qjsc_repl_size;

extern "C" const uint8_t qjsc_repl_cc[];
extern "C" const uint32_t qjsc_repl_cc_size;

int ConsoleMain(int argc, const char **argv) {
  CommandLineOptions options;
  std::vector<std::string> params;

  if (!ParseCommandLine(argc, argv, &options, &params).ok()) {
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

  if (!ctx.InitBuiltins(options.fidl_ir_path[0], options.boot_js_lib_path[0])) {
    ctx.DumpError();
    return 1;
  }

  JSContext *ctx_ptr = ctx.Get();

  // TODO(jeremymanson): The second and third parameter below let you define properties on the
  // command line, which might be nice at some point.
  js_std_add_helpers(ctx_ptr, 0, nullptr);

  if (!options.command_string) {
    if (!options.line_editor) {
      // Use the qjs repl for the time being.
      js_std_eval_binary(ctx_ptr, qjsc_repl, qjsc_repl_size, 0);
    } else {
      if (li::LiModuleInit(ctx_ptr, "li_internal") == nullptr) {
        ctx.DumpError();
        return 1;
      }
      js_std_eval_binary(ctx_ptr, qjsc_repl_cc, qjsc_repl_cc_size, 0);
    }
  } else {
    const char *command = options.command_string->c_str();
    JSValue result = JS_Eval(ctx_ptr, command, options.command_string->length(), "batch", 0);
    if (JS_IsException(result)) {
      ctx.DumpError();
      return 1;
    }
  }
  js_std_loop(ctx_ptr);

  return 0;
}

}  // namespace shell
