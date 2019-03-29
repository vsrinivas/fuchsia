// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/fakes/fake_view.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/lib/fxl/logging.h"

namespace media_player {
namespace test {

FakeView::FakeView()
    : dispatcher_(async_get_default_dispatcher()),
      binding_(this),
      service_provider_binding_(this) {}

FakeView::~FakeView() {}

void FakeView::Bind(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
    zx::eventpair view_token, ::fuchsia::ui::viewsv1::ViewListenerPtr listener,
    zx::eventpair parent_export_token, fidl::StringPtr label) {
  binding_.Bind(std::move(view_request));
  view_listener_ = std::move(listener);
  view_token_ = std::move(view_token);
  parent_export_token_ = std::move(parent_export_token);
  label_ = label;
}

void FakeView::GetServiceProvider(
    fidl::InterfaceRequest<::fuchsia::sys::ServiceProvider> service_provider) {
  service_provider_binding_.Bind(std::move(service_provider));
}

void FakeView::OfferServiceProvider(
    fidl::InterfaceHandle<::fuchsia::sys::ServiceProvider> service_provider,
    std::vector<std::string> service_names) {
  FXL_NOTIMPLEMENTED();
}

void FakeView::GetContainer(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewContainer> container) {
  FXL_NOTIMPLEMENTED();
}

void FakeView::ConnectToService(std::string name, zx::channel channel) {
  FXL_LOG(ERROR) << "ServiceProvider::ConnectToService: name " << name
                 << "  not recognized";
}

}  // namespace test
}  // namespace media_player
