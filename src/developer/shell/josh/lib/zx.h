// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_JOSH_LIB_ZX_H_
#define SRC_DEVELOPER_SHELL_JOSH_LIB_ZX_H_

#include <zircon/status.h>
#include <zircon/types.h>

#include "third_party/quickjs/quickjs.h"

namespace shell {

namespace zx {

// Returns an Error with message
JSValue ZxStatusToError(JSContext *ctx, zx_status_t status);

// Extracts a handle from a given JSValue Handle object.
zx_handle_info_t HandleFromJsval(JSValue val);

// Returns a module that supports syscall functionality.
JSModuleDef *ZxModuleInit(JSContext *ctx, const char *module_name);

// Creates a JSValue of class Handle with the given handle of the given type.
JSValue HandleCreate(JSContext *ctx, zx_handle_t handle, zx_obj_type_t type);

}  // namespace zx

}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_JOSH_LIB_ZX_H_
