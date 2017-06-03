// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/resource.h"

#include "apps/mozart/src/scene/session/session.h"

namespace mozart {
namespace composer {

const ResourceTypeInfo Resource::kTypeInfo = {0, "Resource"};

Resource::Resource(Session* session, const ResourceTypeInfo& type_info)
    : session_(session), type_info_(type_info) {
  FTL_DCHECK(session);
  FTL_DCHECK(type_info.IsKindOf(Resource::kTypeInfo));
  session_->IncrementResourceCount();
}

Resource::~Resource() {
  session_->DecrementResourceCount();
}

ErrorReporter* Resource::error_reporter() const {
  return session_->error_reporter();
}

}  // namespace composer
}  // namespace mozart
