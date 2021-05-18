// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/tests/fakes/fake_scenic.h"

namespace root_presenter {
namespace testing {

FakeScenic::FakeScenic() {}

FakeScenic::~FakeScenic() {}

void FakeScenic::CreateSession(
    fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
  auto& fake_session = fake_sessions_.emplace_back(std::make_unique<FakeSession>());
  fake_session->Bind(std::move(session), listener.Bind());
}

void FakeScenic::CreateSession2(
    fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
    fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) {
  auto& fake_session = fake_sessions_.emplace_back(std::make_unique<FakeSession>());
  fake_session->Bind(std::move(session), listener.Bind());
  auto& fake_focuser = fake_focusers_.emplace_back(std::make_unique<FakeFocuser>());
  fake_focuser->Bind(std::move(view_focuser));
}

void FakeScenic::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  zx::event event;
  zx::event::create(0, &event);
  callback(std::move(event));
}

}  // namespace testing
}  // namespace root_presenter
