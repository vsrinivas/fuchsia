// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/present_view/testing/fake_view.h"

#include <lib/async/default.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/types.h>

#include <functional>

#include "gtest/gtest.h"

namespace present_view::testing {

FakeView::FakeView() : legacy_binding_(this), binding_(this) {}

FakeView::~FakeView() = default;

void FakeView::CreateView(
    zx::eventpair view_token,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*incoming_services*/,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) {
  auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
  CreateViewWithViewRef(std::move(view_token), std::move(view_ref_control), std::move(view_ref));
}

void FakeView::CreateViewWithViewRef(zx::eventpair view_token,
                                     fuchsia::ui::views::ViewRefControl view_ref_control,
                                     fuchsia::ui::views::ViewRef view_ref) {
  // Wait on the passed-in |ViewToken| so we can detect if the peer token is destroyed.
  token_waiter_ =
      std::make_unique<async::Wait>(view_token.get(), __ZX_OBJECT_PEER_CLOSED, 0,
                                    std::bind([this] { token_peer_disconnected_ = true; }));
  token_.value = std::move(view_token);

  zx_status_t wait_status = token_waiter_->Begin(async_get_default_dispatcher());
  EXPECT_EQ(wait_status, ZX_OK);
}

void FakeView::Present(fuchsia::ui::views::ViewToken view_token) {
  auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
  CreateViewWithViewRef(std::move(view_token.value), std::move(view_ref_control),
                        std::move(view_ref));
}

void FakeView::NotImplemented_(const std::string& name) {
  FAIL() << "Unimplemented -- fuchsia.ui.views.ViewProvider::" << name;
}

void FakeView::BindLegacy(fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
  ASSERT_FALSE(bound());
  legacy_binding_.Bind(std::move(request));
}

void FakeView::Bind(fidl::InterfaceRequest<fuchsia::ui::views::View> request) {
  ASSERT_FALSE(bound());
  binding_.Bind(std::move(request));
}

void FakeView::OnKill() {
  token_waiter_.reset();
  legacy_binding_.Unbind();
  binding_.Unbind();
  token_ = fuchsia::ui::views::ViewToken();
  killed_ = true;
}

}  // namespace present_view::testing
