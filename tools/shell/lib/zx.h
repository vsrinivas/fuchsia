// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SHELL_LIB_ZX_H_
#define TOOLS_SHELL_LIB_ZX_H_

#include <zircon/status.h>
#include <zircon/types.h>

#include "third_party/quickjs/quickjs.h"

namespace shell {

namespace zx {

// Returns an Error with message
JSValue ZxStatusToError(JSContext *ctx, zx_status_t status);

// Returns a module that supports syscall functionality.
JSModuleDef *ZxModuleInit(JSContext *ctx, const char *module_name);

}  // namespace zx

}  // namespace shell

#endif  // TOOLS_SHELL_LIB_ZX_H_
