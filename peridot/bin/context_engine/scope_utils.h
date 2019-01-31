// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_SCOPE_UTILS_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_SCOPE_UTILS_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>

namespace maxwell {

fuchsia::modular::ContextSelectorPtr ComponentScopeToContextSelector(
    const fuchsia::modular::ComponentScopePtr& scope);

}  // namespace maxwell

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_SCOPE_UTILS_H_
