// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/wait.h>

#include <unordered_map>

namespace a11y {

// A11yFocusManager class is responsible for storing the node in a11y focus for every view along
// with storing the current view in a11y focus. A11yFocusManager is also responsible for exposing
// methods to set a11y focus to a particular view using fuchsia::ui::views::Focuser protocol, and to
// get current A11y Focus state.
class A11yFocusManager {
 public:
  // Defines which view is currently in a11y focus along with the node_id of the node inside that
  // view.
  struct A11yFocusInfo {
    zx_koid_t view_ref_koid;
    uint32_t node_id;
  };

  // Callback which will be used to notify that an error is encountered while trying to set a11y
  // focus.
  using SetA11yFocusCallback = fit::function<void(bool)>;

  // Root node id, which will be used to set the default node_id for a view.
  static constexpr uint32_t kRootNodeId = 0;

  // 1. FocuserPtr: Client-side channel of the focuser protocol. This is used for requesting
  //    a11y focus on a specific view. Focuser is already instantiated before passing to the
  //    constructor.
  explicit A11yFocusManager(fuchsia::ui::views::FocuserPtr focuser);

  ~A11yFocusManager();

  // Returns A11yFocusInfo of the view which currently has a11y focus. Doesn't return a value if no
  // view has a11y focus.
  std::optional<A11yFocusInfo> GetA11yFocus();

  // Sets the a11y focus to a different node. If the current focused view is different than the one
  // in this request, a focus Chain is first performed to change the focus to the other view.
  // SetA11yFocusCallback is used to notify the called whether the operation is successful or not.
  // SetA11yFocus fails when either the view doesn't match any existing view or if the
  // RequestFocus() is declined.
  void SetA11yFocus(zx_koid_t koid, uint32_t node_id, SetA11yFocusCallback callback);

  // Test-only function to set a11y focus to a specific ViewRef.
  // TODO(fxb/44503): Make this function private when callback is implemented to receive focus chain
  // from FocusChainManager.
  void AddViewRef(fuchsia::ui::views::ViewRef view_ref);

 private:
  // CleanUpRemovedView is called when ViewRef peer is destroyed. It is responsible for deleting all
  // the entries in the hash-map, related to the ViewRef. It also updates the current_focused_view_,
  // if the view in a11y focus is deleted.
  void CleanUpRemovedView(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal* signal);

  // Helper function to initialize WaitMethod() to invoke CleanUpRemovedView() when any view_ref is
  // invalidated.
  void InitializeWaitMethod(zx_koid_t koid);

  // Map for storing node_id which is in a11y focus for every viewref_koid.
  // By default, root-node(node_id = 0) is set for a view in a11y focus.
  std::unordered_map<zx_koid_t /* viewref_koid */, uint32_t /* node_id */>
      focused_node_in_view_map_;

  // Map for accessing view_refs with koid.
  std::unordered_map<zx_koid_t /* viewref_koid */, fuchsia::ui::views::ViewRef /* view_ref */>
      koid_to_viewref_map_;

  // Stores the koid of the view which is currently in a11y focus.
  zx_koid_t currently_focused_view_ = ZX_KOID_INVALID;

  // Map for storing wait method for every viewref.
  std::unordered_map<
      zx_koid_t,
      std::unique_ptr<async::WaitMethod<A11yFocusManager, &A11yFocusManager::CleanUpRemovedView>>>
      wait_map_;

  // Client-side channel of the focuser protocol (fuchsia.ui.views.focuser).
  fuchsia::ui::views::FocuserPtr focuser_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_FOCUS_A11Y_FOCUS_MANAGER_H_
