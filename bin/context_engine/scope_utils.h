// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "apps/maxwell/services/user/scope.fidl.h"
#include "apps/maxwell/services/context/metadata.fidl.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"

namespace maxwell {

ContextSelectorPtr ComponentScopeToContextSelector(const ComponentScopePtr& scope);

}  // namespace maxwell
