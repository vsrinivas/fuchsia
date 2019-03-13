// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_STARTUP_CONTEXT_H_
#define LIB_SYS_CPP_STARTUP_CONTEXT_H_

#include <lib/sys/cpp/component_context.h>

namespace sys {

// TODO: Remove once all clients migrate.
using StartupContext = ComponentContext;

}  // namespace sys

#endif  // LIB_SYS_CPP_STARTUP_CONTEXT_H_
