// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_impl2.h"

namespace view_manager {

ViewImpl2::ViewImpl2(ViewRegistry2* registry, mozart2::SessionPtr session)
    : ViewImpl(registry), session_(std::move(session)) {
  FTL_DCHECK(session_);
}

void ViewImpl2::OnSetState() {
  auto registry = registry_;
  auto state = state_;
  session_.set_connection_error_handler([registry, state] {
    registry->OnViewDied(state, "View Session connection closed");
  });
  PopulateSession();
}

void ViewImpl2::PopulateSession() {
  // TODO: Add initial Link.
}

void ViewImpl2::CreateSession(
    ::fidl::InterfaceRequest<mozart2::Session> session,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  session_->Connect(std::move(session), std::move(listener));
}

}  // namespace view_manager
