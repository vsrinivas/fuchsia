// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime.h"

#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "src/developer/shell/josh/lib/fdio.h"
#include "src/developer/shell/josh/lib/fidl.h"
#include "src/developer/shell/josh/lib/fxlog.h"
#include "src/developer/shell/josh/lib/zx.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace fs = std::filesystem;

// This is present and needs to be called in the most recent version of quickjs.
// It's only here so that we can run against both the old and new version until
// we integrate the new version.
__attribute__((weak)) void js_std_init_handlers(JSRuntime* rt) {}

namespace shell {

Runtime::Runtime() {
  rt_ = JS_NewRuntime();
  js_std_init_handlers(rt_);
  is_valid_ = (rt_ == nullptr);

  // The correct loader for ES6 modules.  Not sure why this has to be done by hand.
  JS_SetModuleLoaderFunc(rt_, nullptr, js_module_loader, nullptr);
}

Runtime::~Runtime() {
  if (is_valid_) {
    js_std_free_handlers(rt_);
    JS_FreeRuntime(rt_);
  }
}

Context::Context(const Runtime* rt) : ctx_(JS_NewContext(rt->Get())) {
  is_valid_ = (ctx_ == nullptr);

#if __has_feature(address_sanitizer)
  // ASan tends to exceed the max stack size of 256K.
  JS_SetMaxStackSize(rt->Get(), 1024 * 1024);
#endif  // __has_feature(address_sanitizer)
}

Context::~Context() {
  if (is_valid_) {
    JS_FreeContext(ctx_);
  }
}
bool Context::ExportScript(const std::string& lib, const std::string& js_path) {
  std::string path;
  int flags = JS_EVAL_TYPE_MODULE;
  if (!js_path.empty()) {
    path = js_path;
  } else {
    path = lib;
    flags |= JS_EVAL_FLAG_COMPILE_ONLY;
  }
  std::string init_str = "import * as " + lib + " from '" + path +
                         "';\n"
                         "globalThis." +
                         lib + " = " + lib + ";\n";

  JSValue init_compile = JS_Eval(ctx_, init_str.c_str(), init_str.length(), "<input>", flags);

  if (JS_IsException(init_compile)) {
    js_std_dump_error(ctx_);
    return false;
  }

  if (js_path.empty()) {
    js_module_set_import_meta(ctx_, init_compile, 1, 1);
    JSValue init_run = JS_EvalFunction(ctx_, init_compile);

    if (JS_IsException(init_run)) {
      js_std_dump_error(ctx_);
      return false;
    }
  }

  return true;
}

bool Context::Export(const std::string& lib, const std::string& js_path) {
  if (js_path.empty()) {
    return ExportScript(lib);
  }

  std::string path = js_path;
  if (path[path.length() - 1] != '/') {
    path.append("/");
  }
  path.append(lib);
  path.append(".js");
  return ExportScript(lib, path);
}

bool Context::InitStd() {
  // System modules
  js_init_module_std(ctx_, "std");
  if (!Export("std")) {
    return false;
  }

  js_init_module_os(ctx_, "os");
  if (!Export("os")) {
    return false;
  }

  return true;
}

extern "C" const uint8_t qjsc_fidl[];
extern "C" const uint32_t qjsc_fidl_size;
extern "C" const uint8_t qjsc_fdio[];
extern "C" const uint32_t qjsc_fdio_size;
extern "C" const uint8_t qjsc_zx[];
extern "C" const uint32_t qjsc_zx_size;
extern "C" const uint8_t qjsc_fxlog[];
extern "C" const uint32_t qjsc_fxlog_size;

bool Context::InitBuiltins(const std::string& fidl_path, const std::string& boot_js_path) {
  if (fxlog::FxLogModuleInit(ctx_, "fxlog_internal") == nullptr) {
    return false;
  }
  js_std_eval_binary(ctx_, qjsc_fxlog, qjsc_fxlog_size, 0);

  if (fdio::FdioModuleInit(ctx_, "fdio") == nullptr) {
    return false;
  }
  if (!Export("fdio")) {
    return false;
  }

  if (fidl::FidlModuleInit(ctx_, "fidl_internal", fidl_path) == nullptr) {
    return false;
  }
  js_std_eval_binary(ctx_, qjsc_fidl, qjsc_fidl_size, 0);

  if (zx::ZxModuleInit(ctx_, "zx_internal") == nullptr) {
    return false;
  }
  js_std_eval_binary(ctx_, qjsc_zx, qjsc_zx_size, 0);

  if (!boot_js_path.empty()) {
    constexpr std::array modules{
        "pp",
        "util",
        "ns",
        "task",
    };

    for (auto& module : modules) {
      if (!Export(std::string(module), boot_js_path)) {
        return false;
      }
    }
  }

  return true;
}

bool Context::InitStartups(const std::string& startup_js_path) {
  if (!fs::is_directory(startup_js_path)) {
    return false;
  }

  fs::path sequence_file = fs::path(startup_js_path) / DEFAULT_SEQUENCE_JSON_FILENAME;
  if (!fs::is_regular_file(sequence_file)) {
    // No sequence.json means nothing to start, which is acceptable
    return true;
  }

  std::ifstream f_seq(sequence_file.string());
  std::stringstream buffer;
  buffer << f_seq.rdbuf();
  f_seq.close();

  rapidjson::Document seq;
  seq.Parse(buffer.str());
  if (!seq.IsObject()) {
    FX_LOGS(ERROR) << "Failed to parse sequence file " << sequence_file;
    return false;
  }

  if (seq.HasMember("startup")) {
    const rapidjson::Value& seq_startup = seq["startup"];
    if (!seq_startup.IsArray()) {
      FX_LOGS(ERROR) << "The 'startup' field needs to be an array";
      return false;
    }

    // Start loading scripts inside startup_js_path based on what is specified
    // in "startup array"
    for (rapidjson::SizeType i = 0; i < seq_startup.Size(); i++) {
      if (!seq_startup[i].IsString()) {
        FX_LOGS(ERROR) << "Wrong type at entry " << i;
        return false;
      }

      fs::path module_path(startup_js_path);
      module_path.append(seq_startup[i].GetString());
      if (!fs::is_regular_file(module_path)) {
        FX_LOGS(ERROR) << "The module script " << module_path << " does not exist";
        return false;
      }

      std::string module_name = module_path.filename().replace_extension("");
      // If the starting chars are digists, strip them because js modules cannot
      // start with digits
      if (!(isupper(module_name[0]) || islower(module_name[0]) || module_name[0] == '_')) {
        FX_LOGS(ERROR) << "Module name " << module_name
                       << " is invalid, can only start with characters or '_'" << module_path;
        return false;
      }

      // Will not continue if any boot script failed to load.
      if (!ExportScript(module_name, module_path.string())) {
        FX_LOGS(ERROR) << "Error occurred while exporting module " << module_path;
        return false;
      }
    }
  }

  return true;
}

}  // namespace shell
