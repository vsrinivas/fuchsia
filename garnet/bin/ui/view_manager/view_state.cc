// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_state.h"

#include "garnet/bin/ui/view_manager/view_impl.h"
#include "garnet/bin/ui/view_manager/view_registry.h"
#include "garnet/bin/ui/view_manager/view_stub.h"
#include "lib/fxl/strings/string_printf.h"

namespace view_manager {

ViewState::ViewState(
    ViewRegistry* registry, uint32_t view_token,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
    ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener,
    scenic::Session* session, const std::string& label)
    : registry_(registry),
      view_token_(view_token),
      view_listener_(std::move(view_listener)),
      top_node_(session),
      label_(label),
      impl_(new ViewImpl(registry, this)),
      view_binding_(impl_.get(), std::move(view_request)),
      weak_factory_(this) {
  FXL_DCHECK(registry_);
  FXL_DCHECK(view_listener_);

  view_binding_.set_error_handler([this](zx_status_t status) {
    registry_->OnViewDied(this, "View connection closed");
  });
  view_listener_.set_error_handler([this](zx_status_t status) {
    registry_->OnViewDied(this, "ViewListener connection closed");
  });
}

ViewState::~ViewState() {}

void ViewState::IssueProperties(
    ::fuchsia::ui::viewsv1::ViewPropertiesPtr properties) {
  issued_properties_ = std::move(properties);
}

void ViewState::BindOwner(ViewLinker::ImportLink owner_link) {
  FXL_DCHECK(owner_link.valid());
  FXL_DCHECK(!owner_link.initialized());

  owner_link_ = std::move(owner_link);
  owner_link_.Initialize(
      this,
      [this](ViewStub* stub) {
        // The peer ViewStub will take care of setting
        // view_stub_ via set_view_stub.  Otherwise,
        // ViewRegistry::HijackView will cause us to be
        // detached from the ViewStub prematurely.
        FXL_VLOG(1) << "View connected: " << this;
      },
      [this] {
        // Ensure the referenced ViewStub is marked nullptr
        // here, as the pointer is invalid at this point as
        // we don't want ViewRegistry::HijackView to use it
        // at all.
        FXL_VLOG(1) << "View disconnected: " << this;
        view_stub_ = nullptr;
        registry_->OnViewDied(this, "ViewHolder connection closed");
      });
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
