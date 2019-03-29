// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_container_state.h"

#include <ostream>

#include "src/lib/fxl/logging.h"

#include "garnet/bin/ui/view_manager/view_registry.h"

#include "lib/ui/scenic/cpp/commands.h"

namespace view_manager {

ViewContainerState::ViewContainerState(ViewRegistry* registry,
                                       fuchsia::ui::scenic::Scenic* scenic)
    : view_registry_(registry), scenic_(scenic) {}

ViewContainerState::~ViewContainerState() {}

void ViewContainerState::AddChild(uint32_t child_key,
                                  zx::eventpair view_holder_token,
                                  zx::eventpair host_import_token) {
  // See if this view was previously transferred
  std::unique_ptr<ChildView> child =
      view_registry_->FindOrphanedView(view_holder_token.get());
  if (child) {
    // Re-using previous ViewHolder.
    FXL_VLOG(1) << "Re-using previously transferred View.";
    child->child_key = child_key;
    child->host_node =
        std::make_unique<scenic::ImportNode>(child->session.get());
  } else {
    scenic::Session* session_ptr = new scenic::Session(scenic_);
    child = std::unique_ptr<ChildView>(
        new ChildView{nullptr, ViewConnectionState::UNKNOWN, view_registry_,
                      child_key, std::unique_ptr<scenic::Session>(session_ptr),
                      std::make_unique<scenic::ImportNode>(session_ptr),
                      scenic::ViewHolder(
                          session_ptr, std::move(view_holder_token),
                          "ViewContainer child=" + std::to_string(child_key))});
  }
  child->session->set_error_handler(
      [view_registry = view_registry_,
       child_ptr = child.get()](zx_status_t status) {
        view_registry->RemoveOrphanedView(child_ptr);
      });
  child->session->set_event_handler(
      [this, child_ptr =
                 child.get()](std::vector<fuchsia::ui::scenic::Event> events) {
        ChildView::OnScenicEvent(child_ptr, std::move(events));
      });
  child->host_node->Bind(std::move(host_import_token));
  child->host_node->Attach(child->view_holder);
  child->session->Present(0, [](fuchsia::images::PresentationInfo info) {});

  if (child->view_connected == ViewConnectionState::CONNECTED) {
    view_registry_->SendChildAttached(this, child->child_key,
                                      fuchsia::ui::viewsv1::ViewInfo());
  } else if (child->view_connected == ViewConnectionState::DISCONNECTED) {
    view_registry_->SendChildUnavailable(this, child->child_key);
  }

  child->container = this;
  children_.insert({child_key, std::move(child)});
}

void ViewContainerState::RemoveChild(uint32_t child_key,
                                     zx::eventpair transferred_view_token) {
  auto child_it = children_.find(child_key);
  FXL_CHECK(child_it != children_.end());

  std::unique_ptr<ChildView> child = std::move(child_it->second);
  child->container = nullptr;
  children_.erase(child_it);

  if (transferred_view_token) {
    child->host_node->DetachChildren();
    child->session->Present(0, [](fuchsia::images::PresentationInfo info) {});
    view_registry_->AddOrphanedView(std::move(transferred_view_token),
                                    std::move(child));
  }
}

void ViewContainerState::RemoveAllChildren() { children_.clear(); }

void ViewContainerState::SetChildProperties(
    uint32_t child_key,
    ::fuchsia::ui::viewsv1::ViewPropertiesPtr child_properties) {
  auto child_it = children_.find(child_key);
  FXL_CHECK(child_it != children_.end());
  auto& child = child_it->second;
  auto view_holder_id = child->view_holder.id();

  bool send_update = false;
  if (child_properties && child_properties->view_layout) {
    auto size = child_properties->view_layout->size;
    auto inset = child_properties->view_layout->inset;

    child->min_dimensions = {0.f, 0.f, 0.f};
    child->max_dimensions = {size.width, size.height, 1000.f};
    child->inset_min = {inset.left, inset.top, 0.f};
    child->inset_max = {-inset.right, -inset.bottom, 0.f};
    send_update = true;
  }

  auto view_properties = scenic::NewSetViewPropertiesCmd(
      view_holder_id, child->min_dimensions.data(),
      child->max_dimensions.data(), child->inset_min.data(),
      child->inset_max.data());

  if (child_properties && child_properties->custom_focus_behavior) {
    view_properties.set_view_properties().properties.focus_change =
        child_properties->custom_focus_behavior->allow_focus;
    send_update = true;
  }

  if (send_update) {
    child->session->Enqueue(std::move(view_properties));
    child->session->Present(0, [](fuchsia::images::PresentationInfo info) {});
  }
}

void ViewContainerState::ChildView::OnScenicEvent(
    ChildView* child, std::vector<fuchsia::ui::scenic::Event> events) {
  for (const auto& event : events) {
    switch (event.Which()) {
      case fuchsia::ui::scenic::Event::Tag::kGfx:
        switch (event.gfx().Which()) {
          case fuchsia::ui::gfx::Event::Tag::kViewConnected: {
            auto view_holder_id = event.gfx().view_connected().view_holder_id;
            FXL_CHECK(child->view_holder.id() == view_holder_id);
            child->view_connected = ViewConnectionState::CONNECTED;
            if (child->container) {
              child->view_registry->SendChildAttached(
                  child->container, child->child_key,
                  fuchsia::ui::viewsv1::ViewInfo());
            }
            break;
          }
          case fuchsia::ui::gfx::Event::Tag::kViewDisconnected: {
            auto view_holder_id =
                event.gfx().view_disconnected().view_holder_id;
            FXL_CHECK(child->view_holder.id() == view_holder_id);
            child->view_connected = ViewConnectionState::DISCONNECTED;
            if (child->container) {
              child->view_registry->SendChildUnavailable(child->container,
                                                         child->child_key);
            }
            break;
          }
          default:
            break;
        }
        break;
      default:
        break;
    }
  }
}

ViewState* ViewContainerState::AsViewState() { return nullptr; }

ViewTreeState* ViewContainerState::AsViewTreeState() { return nullptr; }

std::ostream& operator<<(std::ostream& os, ViewContainerState* state) {
  if (!state)
    return os << "null";
  return os << state->FormattedLabel();
}

}  // namespace view_manager
