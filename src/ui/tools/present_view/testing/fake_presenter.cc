// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/present_view/testing/fake_presenter.h"

#include <lib/async/default.h>
#include <zircon/types.h>

#include <functional>

#include "gtest/gtest.h"

namespace present_view::testing {

FakePresentation::FakePresentation(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request)
    : token_waiter_(
          std::make_unique<async::Wait>(view_holder_token.value.get(), __ZX_OBJECT_PEER_CLOSED, 0,
                                        std::bind([this] { token_peer_disconnected_ = true; }))),
      binding_(this, std::move(presentation_request)),
      token_(std::move(view_holder_token)) {
  zx_status_t wait_status = token_waiter_->Begin(async_get_default_dispatcher());
  EXPECT_EQ(wait_status, ZX_OK);
}

FakePresentation::~FakePresentation() = default;

void FakePresentation::NotImplemented_(const std::string& name) {
  FAIL() << "Unimplemented -- fuchsia.ui.policy.Presentation::" << name;
}

FakePresenter::FakePresenter() : binding_(this) {}

FakePresenter::~FakePresenter() = default;

fidl::InterfaceRequestHandler<fuchsia::ui::policy::Presenter> FakePresenter::GetHandler() {
  return [this](fidl::InterfaceRequest<fuchsia::ui::policy::Presenter> request) {
    ASSERT_FALSE(bound());
    binding_.Bind(std::move(request));
  };
}

void FakePresenter::PresentView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) {
  PresentOrReplaceView(std::move(view_holder_token), std::move(presentation_request));
}

void FakePresenter::PresentOrReplaceView(
    fuchsia::ui::views::ViewHolderToken view_holder_token,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> presentation_request) {
  ASSERT_FALSE(presentation_);
  presentation_.emplace(std::move(view_holder_token), std::move(presentation_request));
}

void FakePresenter::NotImplemented_(const std::string& name) {
  FAIL() << "Unimplemented -- fuchsia.ui.policy.Presenter::" << name;
}

}  // namespace present_view::testing
