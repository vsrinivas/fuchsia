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
  fake_session_.Bind(std::move(session), listener.Bind());
}

void FakeScenic::CreateSession2(
    fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
    fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) {
  fake_session_.Bind(std::move(session), listener.Bind());
  fake_focuser_.Bind(std::move(view_focuser));
}

}  // namespace testing
}  // namespace root_presenter
