// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVELOPER_SHELL_CONSOLE_LI_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_LI_H_

#include "repl.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

namespace shell::li {

repl::Repl *GetRepl(JSContext *ctx, JSValueConst repl_js);
JSModuleDef *LiModuleInit(JSContext *ctx, const char *module_name);

}  // namespace shell::li

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_LI_H_
