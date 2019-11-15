// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SHELL_LIB_FDIO_H_
#define TOOLS_SHELL_LIB_FDIO_H_

#include "third_party/quickjs/quickjs.h"

namespace shell::fdio {

// Returns a module that supports syscall functionality.
JSModuleDef *FdioModuleInit(JSContext *ctx, const char *module_name);

}  // namespace shell::fdio

#endif  // TOOLS_SHELL_LIB_FDIO_H_
