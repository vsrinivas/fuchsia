// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_CONTEXT_ENGINE_SCOPE_UTILS_H_
#define SRC_MODULAR_BIN_CONTEXT_ENGINE_SCOPE_UTILS_H_

#include <fuchsia/modular/cpp/fidl.h>

#include <string>

namespace maxwell {

fuchsia::modular::ContextSelectorPtr ComponentScopeToContextSelector(
    const fuchsia::modular::ComponentScopePtr& scope);

}  // namespace maxwell

#endif  // SRC_MODULAR_BIN_CONTEXT_ENGINE_SCOPE_UTILS_H_
