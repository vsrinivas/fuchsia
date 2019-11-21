// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_LIB_FDIO_H_
#define SRC_DEVELOPER_SHELL_LIB_FDIO_H_

#include "third_party/quickjs/quickjs.h"

namespace shell::fdio {

// Returns a module that supports syscall functionality.
JSModuleDef *FdioModuleInit(JSContext *ctx, const char *module_name);

}  // namespace shell::fdio

#endif  // SRC_DEVELOPER_SHELL_LIB_FDIO_H_
