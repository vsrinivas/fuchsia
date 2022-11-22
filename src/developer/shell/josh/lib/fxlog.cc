// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxlog.h"

#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/macros.h>

#include <cstring>

#include "src/developer/shell/josh/lib/qjs_util.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

#define FX_JS_LOG_STREAM(severity, tag, file, line) \
  ::syslog::LogMessage(severity, file, line, nullptr, tag).stream()

#define FX_JS_LOGST(severity, tag, file, line)                \
  FX_LAZY_STREAM(FX_JS_LOG_STREAM(severity, tag, file, line), \
                 (::syslog::ShouldCreateLogMessage(severity)))

namespace shell::fxlog {

// Dump message to syslog using the given severity.
// There are two types of input:
// Type 1:
//   arg[0] is the message
// Type 2:
//   argv[0] is the tag
//   argv[1] is the message.
//   (optional) argv[2] is the file.
//   (optional) argv[3] is the line.
// Returns the handle of the child.
JSValue WriteLog(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic) {
  if (argc < 1) {
    return JS_ThrowSyntaxError(ctx, "Bad arguments to syslog.error");
  }

  const char *tag = "josh";
  const char *msg = nullptr;
  CStringHolder tag_info(ctx);
  CStringHolder message(ctx);

  if (argc == 1) {
    // Message only
    msg = message.reset(argv[0]);
    if (!msg) {
      return JS_EXCEPTION;
    }
  } else {
    // Tag + message
    tag = tag_info.reset(argv[0]);
    if (!tag) {
      return JS_EXCEPTION;
    }

    msg = message.reset(argv[1]);
    if (!msg) {
      return JS_EXCEPTION;
    }
  }

  const char *file = "";
  CStringHolder file_info(ctx);
  if (argc >= 3) {
    file = file_info.reset(argv[2]);
    if (!file) {
      return JS_EXCEPTION;
    }
  }

  int32_t line = 0;
  if (argc >= 4) {
    if (JS_ToInt32(ctx, &line, argv[3]) == -1) {
      return JS_EXCEPTION;
    }
  }
  FX_JS_LOGST(static_cast<::syslog::LogSeverity>(magic), tag, file, line) << msg;
  return JS_NewInt32(ctx, strlen(msg));
}

JSClassID handle_class_id_;

JSClassDef handle_class_ = {
    .class_name = "Handle",
    .finalizer = nullptr,
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
const JSCFunctionListEntry funcs_[] = {
    /* Fuchsia handle operations */
    JS_CFUNC_MAGIC_DEF("trace", 4, WriteLog, ::syslog::LOG_TRACE),
    JS_CFUNC_MAGIC_DEF("debug", 4, WriteLog, ::syslog::LOG_DEBUG),
    JS_CFUNC_MAGIC_DEF("info", 4, WriteLog, ::syslog::LOG_INFO),
    JS_CFUNC_MAGIC_DEF("warn", 4, WriteLog, ::syslog::LOG_WARNING),
    JS_CFUNC_MAGIC_DEF("error", 4, WriteLog, ::syslog::LOG_ERROR),
    JS_CFUNC_MAGIC_DEF("fatal", 4, WriteLog, ::syslog::LOG_FATAL),
};
#pragma GCC diagnostic pop

namespace {

int FxLogRunOnInit(JSContext *ctx, JSModuleDef *m) {
  JS_NewClassID(&handle_class_id_);
  JS_NewClass(JS_GetRuntime(ctx), handle_class_id_, &handle_class_);
  return JS_SetModuleExportList(ctx, m, funcs_, std::size(funcs_));
}

}  // namespace

JSModuleDef *FxLogModuleInit(JSContext *ctx, const char *module_name) {
  JSModuleDef *m = JS_NewCModule(ctx, module_name, FxLogRunOnInit);
  if (!m) {
    return nullptr;
  }
  JS_AddModuleExportList(ctx, m, funcs_, std::size(funcs_));
  return m;
}

}  // namespace shell::fxlog
