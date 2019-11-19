// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/resource.h"

#include <algorithm>

#include "src/ui/scenic/lib/gfx/engine/session.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Resource::kTypeInfo = {0, "Resource"};

Resource::Resource(Session* session, SessionId session_id, ResourceId id,
                   const ResourceTypeInfo& type_info)
    : session_DEPRECATED_(session), global_id_(session_id, id), type_info_(type_info) {
  FXL_DCHECK(type_info.IsKindOf(Resource::kTypeInfo));
  if (session_DEPRECATED_) {
    FXL_DCHECK(session_DEPRECATED_->id() == session_id);
    session_DEPRECATED_->IncrementResourceCount();
  }
}

Resource::~Resource() {
  if (session_DEPRECATED_) {
    session_DEPRECATED_->DecrementResourceCount();
  }
}

EventReporter* Resource::event_reporter() const { return session_DEPRECATED_->event_reporter(); }

const ResourceContext& Resource::resource_context() const {
  return session_DEPRECATED_->resource_context();
}

bool Resource::SetLabel(const std::string& label) {
  label_ = label.substr(0, ::fuchsia::ui::gfx::kLabelMaxLength);
  return true;
}

bool Resource::SetEventMask(uint32_t event_mask) {
  event_mask_ = event_mask;
  return true;
}

bool Resource::Detach(ErrorReporter* error_reporter) {
  error_reporter->ERROR() << "Resources of type: " << type_name() << " do not support Detach().";
  return false;
}

}  // namespace gfx
}  // namespace scenic_impl
