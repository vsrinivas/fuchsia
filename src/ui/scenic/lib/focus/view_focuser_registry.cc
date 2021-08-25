// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/focus/view_focuser_registry.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/utils/helpers.h"

namespace focus {

using ViewRef = fuchsia::ui::views::ViewRef;
using ViewFocuser = fuchsia::ui::views::Focuser;
using RequestFocusCallback = fuchsia::ui::views::Focuser::RequestFocusCallback;

ViewFocuserRegistry::ViewFocuserRegistry(RequestFocusFunc request_focus)
    : request_focus_(std::move(request_focus)) {}

void ViewFocuserRegistry::Register(zx_koid_t view_ref_koid,
                                   fidl::InterfaceRequest<ViewFocuser> view_focuser) {
  endpoints_.try_emplace(
      view_ref_koid, std::move(view_focuser),
      /*error_handler*/
      [this, view_ref_koid](auto) { endpoints_.erase(view_ref_koid); },
      /*request_focus_handler*/
      [this, requestor = view_ref_koid](ViewRef view_ref, RequestFocusCallback response) {
        if (request_focus_(requestor, utils::ExtractKoid(view_ref))) {
          response(fpromise::ok());  // Request received, and honored.
          return;
        }

        response(fpromise::error(fuchsia::ui::views::Error::DENIED));  // Report a problem.
      });
}

ViewFocuserRegistry::ViewFocuserEndpoint::ViewFocuserEndpoint(
    fidl::InterfaceRequest<ViewFocuser> view_focuser,
    fit::function<void(zx_status_t)> error_handler,
    fit::function<void(ViewRef, RequestFocusCallback)> request_focus_handler)
    : request_focus_handler_(std::move(request_focus_handler)),
      endpoint_(this, std::move(view_focuser)) {
  FX_DCHECK(error_handler) << "invariant";
  FX_DCHECK(request_focus_handler_) << "invariant";
  endpoint_.set_error_handler(std::move(error_handler));
}

void ViewFocuserRegistry::ViewFocuserEndpoint::RequestFocus(ViewRef view_ref,
                                                            RequestFocusCallback response) {
  request_focus_handler_(std::move(view_ref), std::move(response));
}

}  // namespace focus
