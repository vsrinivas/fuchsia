// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/a11y_view.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

namespace a11y {

AccessibilityView::AccessibilityView(
    fuchsia::ui::accessibility::view::RegistryPtr accessibility_view_registry,
    fuchsia::ui::scenic::ScenicPtr scenic)
    : accessibility_view_registry_(std::move(accessibility_view_registry)),
      scenic_(std::move(scenic)),
      session_(scenic_.get()) {
  // Set up session listener event handler.
  session_.set_event_handler(
      [this](std::vector<fuchsia::ui::scenic::Event> events) { OnScenicEvent(std::move(events)); });

  accessibility_view_registry_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::accessibility::view::Registry"
                   << zx_status_get_string(status);
  });

  // Create view token and view ref pairs for a11y view.
  auto [a11y_view_token, a11y_view_holder_token] = scenic::ViewTokenPair::New();
  auto [a11y_control_ref, a11y_view_ref] = scenic::ViewRefPair::New();

  // Make a copy of the a11y view ref. We need an extra copy to send to root
  // presenter so that it can route input through the a11y view.
  fuchsia::ui::views::ViewRef a11y_view_ref_copy;
  fidl::Clone(a11y_view_ref, &a11y_view_ref_copy);

  // Create a11y view. We need to do this step before we ask root presenter to
  // add our view holder to the scene, because root presenter will try to route
  // input to the a11y view at that time. If the a11y view does not yet exist,
  // that step will fail.
  a11y_view_.emplace(&session_, std::move(a11y_view_token), std::move(a11y_control_ref),
                     std::move(a11y_view_ref), "A11y View");

  // TODO(fxbug.dev/77045): Switch to use SafePresenter.
  // Apply changes.
  session_.Present(
      /* presentation_time = */ 0,
      /* presentation_callback = */ [this, a11y_view_ref_copy = std::move(a11y_view_ref_copy),
                                     a11y_view_holder_token = std::move(a11y_view_holder_token)](
                                        fuchsia::images::PresentationInfo info) mutable {
        // Insert a11y view into root presenter.
        accessibility_view_registry_->CreateAccessibilityViewHolder(
            std::move(a11y_view_ref_copy), std::move(a11y_view_holder_token),
            [this](fuchsia::ui::views::ViewHolderToken proxy_view_holder_token) {
              // Create the proxy view holder and attach it to the scene.
              proxy_view_holder_.emplace(&session_, std::move(proxy_view_holder_token),
                                         "Proxy View Holder");
              a11y_view_->AddChild(proxy_view_holder_.value());

              // If the a11y view is already attached to the scene, use its properties
              // to set the properties of the proxy view holder. Otherwise, let
              // |OnScenicEvent| set the proxy view holder properties when the a11y
              // view is attached.
              if (a11y_view_properties_) {
                session_.Enqueue(scenic::NewSetViewPropertiesCmd(proxy_view_holder_->id(),
                                                                 *a11y_view_properties_));
              }

              // Apply changes.
              session_.Present(
                  /* presentation_time = */ 0,
                  /* presentation_callback = */ [](fuchsia::images::PresentationInfo info) {});
            });
      });
}

void AccessibilityView::OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) {
  bool changes_to_present = false;
  for (const auto& event : events) {
    if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx) {
      const auto& gfx_event = event.gfx();
      if (gfx_event.Which() == fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene) {
        const auto& view_attached_event = gfx_event.view_attached_to_scene();
        if (view_attached_event.view_id == a11y_view_->id()) {
          a11y_view_properties_ = view_attached_event.properties;
          // If the client view holder was already created, then we need to set
          // its properties.
          if (proxy_view_holder_) {
            session_.Enqueue(
                scenic::NewSetViewPropertiesCmd(proxy_view_holder_->id(), *a11y_view_properties_));
            changes_to_present = true;
          }
        }
      } else if (gfx_event.Which() == fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged) {
        const auto& view_properties_changed_event = gfx_event.view_properties_changed();
        if (view_properties_changed_event.view_id == a11y_view_->id()) {
          a11y_view_properties_ = view_properties_changed_event.properties;
          // If the client view holder was already created, then we need to set
          // its properties.
          if (proxy_view_holder_) {
            session_.Enqueue(
                scenic::NewSetViewPropertiesCmd(proxy_view_holder_->id(), *a11y_view_properties_));
            changes_to_present = true;
          }
        }
      }
    }
  }

  if (changes_to_present) {
    session_.Present(
        /* presentation_time = */ 0,
        /* presentation_callback = */ [](fuchsia::images::PresentationInfo info) {});
  }
}

}  // namespace a11y
