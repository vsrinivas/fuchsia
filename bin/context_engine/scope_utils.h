// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "lib/user_intelligence/fidl/scope.fidl.h"
#include "lib/context/fidl/metadata.fidl.h"
#include "lib/context/fidl/context_reader.fidl.h"

namespace maxwell {

ContextSelectorPtr ComponentScopeToContextSelector(const ComponentScopePtr& scope);

}  // namespace maxwell
