// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef TOOLS_SHELL_LIB_LI_H_
#define TOOLS_SHELL_LIB_LI_H_

#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

namespace shell::li {

JSModuleDef *LiModuleInit(JSContext *ctx, const char *module_name);

}  // namespace shell::li

#endif  // TOOLS_SHELL_LIB_LI_H_
