// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SHELL_LIB_RUNTIME_H_
#define TOOLS_SHELL_LIB_RUNTIME_H_

#include <string>

#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

namespace shell {

// A C++ wrapper for the JSRuntime type.  Creates a JSRuntime and manages its lifetime.
class Runtime {
 public:
  Runtime();

  JSRuntime* Get() const { return rt_; }

  ~Runtime();

 private:
  JSRuntime* rt_;
  bool is_valid_;
};

// A C++ wrapper for the JSContext type.  You can have multiple JSContexts for a given JSRuntime.
// Creates a JSContext and manages its lifetime.
class Context {
 public:
  Context(const Runtime* rt);
  ~Context();

  JSContext* Get() const { return ctx_; }
  void DumpError() { js_std_dump_error(ctx_); }

  // Initializes standard libc functions and makes them available via globalThis.
  bool InitStd();

  // Initialize Fuchsia-isms: zx_internal, fdio, etc.
  // fidl_path is the directory to look for the FIDL IR.
  bool InitBuiltins(const std::string& fidl_path);

 private:
  JSContext* ctx_;
  bool is_valid_;

  bool Export(const std::string&);
};

}  // namespace shell

#endif  // TOOLS_SHELL_LIB_RUNTIME_H_
