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
using SetAutoFocusCallback = fuchsia::ui::views::Focuser::SetAutoFocusCallback;

ViewFocuserRegistry::ViewFocuserRegistry(RequestFocusFunc request_focus,
                                         SetAutoFocusFunc set_auto_focus)
    : request_focus_(std::move(request_focus)), set_auto_focus_(std::move(set_auto_focus)) {}

void ViewFocuserRegistry::Register(zx_koid_t view_ref_koid,
                                   fidl::InterfaceRequest<ViewFocuser> view_focuser) {
  endpoints_.try_emplace(
      view_ref_koid, std::move(view_focuser),
      /*error_handler*/
      [this, view_ref_koid](auto) {
        set_auto_focus_(view_ref_koid, ZX_KOID_INVALID);
        endpoints_.erase(view_ref_koid);
      },
      /*request_focus*/
      [this, requestor = view_ref_koid](ViewRef view_ref, RequestFocusCallback response) {
        if (request_focus_(requestor, utils::ExtractKoid(view_ref))) {
          response(fpromise::ok());  // Request received, and honored.
          return;
        }

        response(fpromise::error(fuchsia::ui::views::Error::DENIED));  // Report a problem.
      },
      /*set_auto_focus*/
      [this, requestor = view_ref_koid](zx_koid_t view_ref_koid) {
        set_auto_focus_(requestor, view_ref_koid);
      });
}

ViewFocuserRegistry::ViewFocuserEndpoint::ViewFocuserEndpoint(
    fidl::InterfaceRequest<ViewFocuser> view_focuser,
    fit::function<void(zx_status_t)> error_handler,
    fit::function<void(ViewRef, RequestFocusCallback)> request_focus,
    fit::function<void(zx_koid_t)> set_auto_focus)
    : request_focus_(std::move(request_focus)),
      set_auto_focus_(std::move(set_auto_focus)),
      endpoint_(this, std::move(view_focuser)) {
  FX_DCHECK(error_handler) << "invariant";
  FX_DCHECK(request_focus_) << "invariant";
  FX_DCHECK(set_auto_focus_) << "invariant";
  endpoint_.set_error_handler(std::move(error_handler));
}

void ViewFocuserRegistry::ViewFocuserEndpoint::RequestFocus(ViewRef view_ref,
                                                            RequestFocusCallback response) {
  request_focus_(std::move(view_ref), std::move(response));
}

void ViewFocuserRegistry::ViewFocuserEndpoint::SetAutoFocus(
    fuchsia::ui::views::FocuserSetAutoFocusRequest request, SetAutoFocusCallback response) {
  zx_koid_t target = ZX_KOID_INVALID;
  if (request.has_view_ref()) {
    target = utils::ExtractKoid(request.view_ref());
  }
  set_auto_focus_(target);
  response(fpromise::ok());
}

}  // namespace focus
