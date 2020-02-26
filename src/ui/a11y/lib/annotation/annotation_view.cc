// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/annotation/annotation_view.h"

#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

AnnotationView::AnnotationView(sys::ComponentContext* component_context,
                               a11y::ViewManager* view_manager, zx_koid_t client_view_koid)
    : view_manager_(view_manager),
      client_view_koid_(client_view_koid),
      session_listener_binding_(this) {
  fuchsia::ui::scenic::ScenicPtr scenic =
      component_context->svc()->Connect<fuchsia::ui::scenic::Scenic>();

  // Create a Scenic Session and a Scenic SessionListener.
  scenic->CreateSession(session_.NewRequest(), session_listener_binding_.NewBinding());

  // Connect to Scenic annotation registry service.
  annotation_registry_ = component_context->svc()->Connect<fuchsia::ui::annotation::Registry>();
  annotation_registry_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::annotation::Registry"
                   << zx_status_get_string(status);
  });
}

void AnnotationView::InitializeView(fuchsia::ui::views::ViewRef client_view_ref) {
  FX_CHECK(client_view_ref.reference);

  std::vector<fuchsia::ui::scenic::Command> cmds;

  // Create view token pair for annotation view and view holder.
  auto [annotation_view_token, annotation_view_holder_token] = scenic::ViewTokenPair::New();

  // Register annotation view holder with scenic.
  annotation_registry_->CreateAnnotationViewHolder(
      std::move(client_view_ref), std::move(annotation_view_holder_token),
      [this]() { state_.annotation_view_registered = true; });

  // TODO: Create annotation view in the session, add create node structure to hold annotations, and
  // present.
}

void AnnotationView::HighlightNode(uint32_t node_id) {
  // TODO: Implement.
}

void AnnotationView::DetachViewContents() {
  // TODO: Implement.
}

void AnnotationView::DrawHighlight(const fuchsia::accessibility::semantics::Node* node) {
  // TODO: Implement.
}

void AnnotationView::OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) {
  // TODO: Implement.
}

void AnnotationView::HandleGfxEvent(const fuchsia::ui::gfx::Event& event) {
  // TODO: Implement.
}

}  // namespace a11y
