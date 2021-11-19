// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FOCUS_VIEW_REF_FOCUSED_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_FOCUS_VIEW_REF_FOCUSED_REGISTRY_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/types.h>

#include <optional>
#include <unordered_map>

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace focus {

// An object for managing fuchsia.ui.views.ViewRefFocused lifecycle, starting with FIDL requests and
// ending with cleanup.
class ViewRefFocusedRegistry {
 public:
  // Stores a FIDL request for fuchsia.ui.views.ViewRefFocused to |pending_requests_|.
  // Pre: |session_id| is unassociated with any fuchsia.ui.views.ViewRefFocused.
  void Register(zx_koid_t view_ref_koid,
                fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> view_focuser);

  // Binds and registers endpoints in |pending_requests_| if its |view_ref_koid| is present in
  // |snapshot|. Remove and destroy any registered endpoint not present in |snapshot|.
  void Update(const view_tree::Snapshot& snapshot);

  // Focus changed, update state.
  void UpdateFocus(zx_koid_t old_focus, zx_koid_t new_focus);

 private:
  class Endpoint : public fuchsia::ui::views::ViewRefFocused {
   public:
    explicit Endpoint(fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> view_focuser);
    Endpoint(Endpoint&& original) noexcept;  // for emplace

    // |fuchsia.ui.views.ViewRefFocused|
    void Watch(fuchsia::ui::views::ViewRefFocused::WatchCallback callback) override;

    // Track focus changes for this ViewRef.
    void UpdateFocus(bool focused);

   private:
    // The accumulated focus changes associated with a view ref.
    // - If empty: no focus change to report.
    // - Otherwise, the next response callback should read then clear this field.
    std::optional<bool> focused_state_;

    // The response action is stored here, if the last |Watch| call did not immediately issue a
    // response (i.e., there was not a focus change to report). A subsequent focus change should
    // trigger the response callback, and clear this field.
    fuchsia::ui::views::ViewRefFocused::WatchCallback response_;

    // Server-side endpoint.
    fidl::Binding<fuchsia::ui::views::ViewRefFocused> endpoint_;
  };

  // Associates ViewRef KOID to ViewRefFocused server-side endpoint.
  std::unordered_map<zx_koid_t, Endpoint> endpoints_;

  // Holds unbound ViewRefFocused endpoints. An endpoint here is moved out to the registration map,
  // |endpoints_|, when the client calls Present() on its view.
  std::unordered_map<zx_koid_t, fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused>>
      pending_requests_;
};

}  // namespace focus

#endif  // SRC_UI_SCENIC_LIB_FOCUS_VIEW_REF_FOCUSED_REGISTRY_H_
