// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_focuser.h"

#include "fuchsia/ui/views/cpp/fidl.h"
#include "lib/fit/result.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

void MockFocuser::RequestFocus(fuchsia::ui::views::ViewRef view_ref,
                               RequestFocusCallback callback) {
  focus_request_received_ = true;
  view_ref_ = std::move(view_ref);
  if (throw_error_) {
    callback(fit::error(fuchsia::ui::views::Error::DENIED));
  } else {
    callback(fit::ok());
  }
}

void MockFocuser::SetFocusRequestReceived(bool focus_received) {
  focus_request_received_ = focus_received;
}

bool MockFocuser::GetFocusRequestReceived() const { return focus_request_received_; }

zx_koid_t MockFocuser::GetViewRefKoid() const { return a11y::GetKoid(view_ref_); }

void MockFocuser::SetThrowError(bool throw_error) { throw_error_ = throw_error; }

}  // namespace accessibility_test
