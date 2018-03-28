// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <fuchsia/cpp/modular.h>

namespace maxwell {

modular::ContextSelectorPtr ComponentScopeToContextSelector(
    const modular::ComponentScopePtr& scope);

}  // namespace maxwell
