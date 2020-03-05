// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/tests/fakes/fake_focuser.h"

#include "src/lib/fxl/logging.h"

namespace root_presenter {
namespace testing {

FakeFocuser::FakeFocuser() : binding_(this) {}

FakeFocuser::~FakeFocuser() {}

void FakeFocuser::Bind(fidl::InterfaceRequest<fuchsia::ui::views::Focuser> request) {
  binding_.Bind(std::move(request));
}

void FakeFocuser::RequestFocus(fuchsia::ui::views::ViewRef view_ref,
                               RequestFocusCallback callback) {
  fuchsia::ui::views::Focuser_RequestFocus_Result result;
  result.set_response(fuchsia::ui::views::Focuser_RequestFocus_Response{});
  callback(std::move(result));
}

}  // namespace testing
}  // namespace root_presenter
