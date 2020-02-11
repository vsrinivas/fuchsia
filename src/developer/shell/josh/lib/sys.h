// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_JOSH_LIB_SYS_H_
#define SRC_DEVELOPER_SHELL_JOSH_LIB_SYS_H_

#include <string>

#include "third_party/quickjs/quickjs.h"

namespace shell::sys {

// Returns a module that supports syscall functionality.
// fidl_path is where we find FIDL IR definitions.
JSModuleDef *SysModuleInit(JSContext *ctx, const char *module_name);

}  // namespace shell::sys

#endif  // SRC_DEVELOPER_SHELL_JOSH_LIB_SYS_H_
