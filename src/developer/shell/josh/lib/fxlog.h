// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_JOSH_LIB_FXLOG_H_
#define SRC_DEVELOPER_SHELL_JOSH_LIB_FXLOG_H_

#include "third_party/quickjs/quickjs.h"

namespace shell::fxlog {

// Returns a module that supports fxlog functionality.
JSModuleDef *FxLogModuleInit(JSContext *ctx, const char *module_name);

}  // namespace shell::fxlog

#endif  // SRC_DEVELOPER_SHELL_JOSH_LIB_FXLOG_H_
