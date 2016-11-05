// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_state.h"

#include "apps/mozart/glue/base/logging.h"
#include "apps/mozart/src/view_manager/view_impl.h"
#include "apps/mozart/src/view_manager/view_registry.h"
#include "apps/mozart/src/view_manager/view_stub.h"
#include "lib/ftl/strings/string_printf.h"

namespace view_manager {

ViewState::ViewState(ViewRegistry* registry,
                     mozart::ViewTokenPtr view_token,
                     fidl::InterfaceRequest<mozart::View> view_request,
                     mozart::ViewListenerPtr view_listener,
                     const std::string& label)
    : view_token_(std::move(view_token)),
      view_listener_(std::move(view_listener)),
      label_(label),
      impl_(new ViewImpl(registry, this)),
      view_binding_(impl_.get(), std::move(view_request)),
      owner_binding_(impl_.get()),
      weak_factory_(this) {
  FTL_DCHECK(view_token_);
  FTL_DCHECK(view_listener_);

  view_binding_.set_connection_error_handler([this, registry] {
    registry->OnViewDied(this, "View connection closed");
  });
  owner_binding_.set_connection_error_handler([this, registry] {
    registry->OnViewDied(this, "ViewOwner connection closed");
  });
  view_listener_.set_connection_error_handler([this, registry] {
    registry->OnViewDied(this, "ViewListener connection closed");
  });
}

ViewState::~ViewState() {}

void ViewState::IssueProperties(mozart::ViewPropertiesPtr properties) {
  issued_scene_version_++;
  FTL_CHECK(issued_scene_version_);
  issued_properties_ = std::move(properties);
}

void ViewState::BindOwner(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_DCHECK(!owner_binding_.is_bound());
  owner_binding_.Bind(std::move(view_owner_request));
}

void ViewState::ReleaseOwner() {
  FTL_DCHECK(owner_binding_.is_bound());
  owner_binding_.Close();
}

ViewState* ViewState::AsViewState() {
  return this;
}

const std::string& ViewState::FormattedLabel() const {
  if (formatted_label_cache_.empty()) {
    formatted_label_cache_ =
        label_.empty()
            ? ftl::StringPrintf("<V%d>", view_token_->value)
            : ftl::StringPrintf("<V%d:%s>", view_token_->value, label_.c_str());
  }
  return formatted_label_cache_;
}

std::ostream& operator<<(std::ostream& os, ViewState* view_state) {
  if (!view_state)
    return os << "null";
  return os << view_state->FormattedLabel();
}

}  // namespace view_manager
