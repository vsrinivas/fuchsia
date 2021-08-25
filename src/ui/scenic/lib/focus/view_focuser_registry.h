// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FOCUS_VIEW_FOCUSER_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_FOCUS_VIEW_FOCUSER_REGISTRY_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/types.h>

#include <unordered_map>
#include <unordered_set>

namespace focus {

using RequestFocusFunc = fit::function<bool(/*requestor*/ zx_koid_t, /*request*/ zx_koid_t)>;

// An object for managing fuchsia.ui.views.Focuser lifecycle, starting with FIDL requests and
// ending with cleanup when the client-side channel closes.
class ViewFocuserRegistry {
 public:
  explicit ViewFocuserRegistry(RequestFocusFunc request_focus);

  // Because this object captures its "this" pointer in internal closures, it is unsafe to copy or
  // move it. Disable all copy and move operations.
  ViewFocuserRegistry(const ViewFocuserRegistry&) = delete;
  ViewFocuserRegistry& operator=(const ViewFocuserRegistry&) = delete;
  ViewFocuserRegistry(ViewFocuserRegistry&&) = delete;
  ViewFocuserRegistry& operator=(ViewFocuserRegistry&&) = delete;

  // Bind a FIDL request for fuchsia.ui.views.Focuser, associated with |view_ref_koid|.
  void Register(zx_koid_t view_ref_koid,
                fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser);

  // For tests.
  std::unordered_set<zx_koid_t> endpoints() const {
    std::unordered_set<zx_koid_t> out;
    std::for_each(endpoints_.begin(), endpoints_.end(),
                  [&](const auto& kv) { out.insert(kv.first); });
    return out;
  }

 private:
  class ViewFocuserEndpoint : public fuchsia::ui::views::Focuser {
   public:
    ViewFocuserEndpoint(
        fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser,
        fit::function<void(zx_status_t)> error_handler,
        fit::function<void(fuchsia::ui::views::ViewRef, RequestFocusCallback)> request_focus);

    // |fuchsia.ui.views.Focuser|
    void RequestFocus(fuchsia::ui::views::ViewRef view_ref, RequestFocusCallback response) override;

   private:
    const fit::function<void(fuchsia::ui::views::ViewRef, RequestFocusCallback)>
        request_focus_handler_;
    fidl::Binding<fuchsia::ui::views::Focuser> endpoint_;
  };

  std::unordered_map<zx_koid_t, ViewFocuserEndpoint> endpoints_;

  const RequestFocusFunc request_focus_;
};

}  // namespace focus

#endif  // SRC_UI_SCENIC_LIB_FOCUS_VIEW_FOCUSER_REGISTRY_H_
