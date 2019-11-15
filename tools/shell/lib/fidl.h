// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SHELL_LIB_FIDL_H_
#define TOOLS_SHELL_LIB_FIDL_H_

#include <string>

#include "third_party/quickjs/quickjs.h"

namespace shell::fidl {

// Returns a module that supports syscall functionality.
// fidl_path is where we find FIDL IR definitions.
JSModuleDef *FidlModuleInit(JSContext *ctx, const char *module_name, const std::string &fidl_path);

}  // namespace shell::fidl

#endif  // TOOLS_SHELL_LIB_FIDL_H_
