// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_state.h"

#include <algorithm>

#include "garnet/bin/ui/view_manager/view_impl.h"
#include "garnet/bin/ui/view_manager/view_registry.h"
#include "garnet/bin/ui/view_manager/view_stub.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace view_manager {

ViewState::ViewState(
    ViewRegistry* registry, uint32_t view_token,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
    ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener,
    zx::eventpair scenic_view_token, zx::eventpair parent_export_token,
    fuchsia::ui::scenic::Scenic* scenic, const std::string& label)
    : ViewContainerState(registry, scenic),
      registry_(registry),
      view_token_(view_token),
      view_listener_(std::move(view_listener)),
      label_(label),
      session_(scenic),
      top_node_(std::in_place, &session_),
      scenic_view_(std::in_place, &session_, std::move(scenic_view_token),
                   FormattedLabel()),
      impl_(new ViewImpl(registry, this)),
      view_binding_(impl_.get(), std::move(view_request)),
      weak_factory_(this) {
  FXL_DCHECK(registry_);
  FXL_DCHECK(view_listener_);

  session_.set_error_handler([this](zx_status_t status) {
    registry_->OnViewDied(this,
                          "view_manager::ViewState: Session connection closed");
  });

  session_.set_event_handler(fit::bind_member(this, &ViewState::OnScenicEvent));

  view_binding_.set_error_handler([this](zx_status_t status) {
    registry_->OnViewDied(this, "View connection closed");
  });
  view_listener_.set_error_handler([this](zx_status_t status) {
    registry_->OnViewDied(this, "ViewListener connection closed");
  });

  scenic_view_->AddChild(*top_node_);

  // Export a node which represents the view's attachment point.
  top_node_->Export(std::move(parent_export_token));
  top_node_->SetTag(view_token_);
  top_node_->SetLabel("ViewState" + FormattedLabel());

  // TODO(SCN-371): Avoid Z-fighting by introducing a smidgen of elevation
  // between each view and its embedded sub-views.  This is not a long-term fix.
  top_node_->SetTranslation(0.f, 0.f, -0.1f);

  session_.Present(0, [](fuchsia::images::PresentationInfo info) {});
}

ViewState::~ViewState() {}

void ViewState::OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) {
  for (const auto& event : events) {
    switch (event.Which()) {
      case fuchsia::ui::scenic::Event::Tag::kGfx:
        switch (event.gfx().Which()) {
          case fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged: {
            auto v2props = event.gfx().view_properties_changed().properties;

            ::fuchsia::ui::viewsv1::ViewProperties v1props;
            v1props.view_layout = ::fuchsia::ui::viewsv1::ViewLayout::New();
            v1props.view_layout->size.width =
                v2props.bounding_box.max.x - v2props.bounding_box.min.x;
            v1props.view_layout->size.height =
                v2props.bounding_box.max.y - v2props.bounding_box.min.y;

            view_listener()->OnPropertiesChanged(std::move(v1props), []() {});
            break;
          }
          case fuchsia::ui::gfx::Event::Tag::kViewHolderDisconnected:
            registry_->OnViewDied(this, "View connection closed");
            break;
          default:
            // Do nothing.
            break;
        }
        break;
      default:
        // Do nothing.
        break;
    }
  }
}

void ViewState::ReleaseScenicResources() {
  top_node_->Detach();
  top_node_.reset();
  scenic_view_.reset();
  session_.Present(0, [](fuchsia::images::PresentationInfo info) {});
}

ViewState* ViewState::AsViewState() { return this; }

const std::string& ViewState::FormattedLabel() const {
  if (formatted_label_cache_.empty()) {
    formatted_label_cache_ =
        label_.empty()
            ? fxl::StringPrintf("<V%d>", view_token_)
            : fxl::StringPrintf("<V%d:%s>", view_token_, label_.c_str());
  }
  return formatted_label_cache_;
}

fuchsia::sys::ServiceProvider* ViewState::GetServiceProviderIfSupports(
    std::string service_name) {
  auto& v = service_names_;
  if (std::find(v.begin(), v.end(), service_name) != v.end()) {
    return service_provider_.get();
  }
  return nullptr;
}

void ViewState::SetServiceProvider(
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> service_provider,
    std::vector<std::string> service_names) {
  if (service_provider) {
    service_provider_ = service_provider.Bind();
    service_names_ = std::move(service_names);

  } else {
    service_provider_.Unbind();
    service_names_.clear();
  }
}

std::ostream& operator<<(std::ostream& os, ViewState* view_state) {
  if (!view_state)
    return os << "null";
  return os << view_state->FormattedLabel();
}

}  // namespace view_manager
