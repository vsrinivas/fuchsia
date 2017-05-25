// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_registry2.h"

#include "apps/mozart/src/view_manager/view_impl2.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

namespace view_manager {

ViewRegistry2::ViewRegistry2(app::ApplicationContext* application_context,
                             mozart2::ComposerPtr composer)
    : ViewRegistry(application_context), composer_(std::move(composer)) {}

void ViewRegistry2::CreateScene(ViewState* view_state,
                                fidl::InterfaceRequest<mozart::Scene> scene) {
  FTL_LOG(ERROR) << "ViewRegistry2::CreateScene() unimplemented.";
  FTL_CHECK(false);
}

void ViewRegistry2::AttachResolvedViewAndNotify(ViewStub* view_stub,
                                                ViewState* view_state) {
  FTL_LOG(ERROR)
      << "ViewRegistry2::AttachResolvedViewAndNotify() unimplemented.";
  FTL_CHECK(false);
}

std::unique_ptr<ViewImpl> ViewRegistry2::CreateViewImpl() {
  mozart2::SessionPtr session;
  composer_->CreateSession(session.NewRequest(), nullptr);

  return std::make_unique<ViewImpl2>(this, std::move(session));
}

}  // namespace view_manager
