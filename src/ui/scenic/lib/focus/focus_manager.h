// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FOCUS_FOCUS_MANAGER_H_
#define SRC_UI_SCENIC_LIB_FOCUS_FOCUS_MANAGER_H_

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

#include "lib/inspect/cpp/inspect.h"
#include "src/ui/scenic/lib/focus/view_ref_focused_registry.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace focus {

// Provide detail on if/why focus change request was denied.
// Specific error-handling policy is responsibility of caller.
enum class FocusChangeStatus {
  kAccept = 0,
  kErrorRequestorInvalid,
  kErrorRequestInvalid,
  kErrorRequestorNotAuthorized,
  kErrorRequestorNotRequestAncestor,
  kErrorRequestCannotReceiveFocus,
  kErrorUnhandledCase,  // last
};

// Callback that should receive either the focused koid or ZX_KOID_INVALID every time the focus
// chain updates. Used by GFX to send focus events over the SessionListener.
// TODO(fxbug.dev/64376): Remove when we remove GFX input.
using LegacyFocusListener = fit::function<void(zx_koid_t, zx_koid_t)>;

// Class for tracking focus state.
class FocusManager final : public fuchsia::ui::focus::FocusChainListenerRegistry {
 public:
  explicit FocusManager(
      inspect::Node inspect_node = inspect::Node(),
      LegacyFocusListener legacy_focus_listener = [](auto, auto) {});
  FocusManager(FocusManager&& other) = delete;  // Disallow moving.

  void Publish(sys::ComponentContext& component_context);

  // Request focus transfer to the proposed ViewRef's KOID |request|, on the behalf of |requestor|.
  // Return kAccept if successful.
  // - If |requestor| is not authorized to focus |request|, return error.
  // - If the |request| is not in |snapshot_.view_tree|, return error.
  // - If the |request| is otherwise valid, but violates the focus transfer policy, return error.
  FocusChangeStatus RequestFocus(zx_koid_t requestor, zx_koid_t request);

  // Saves the new snapshot and updates the focus chain accordingly.
  void OnNewViewTreeSnapshot(std::shared_ptr<const view_tree::Snapshot> snapshot);

  // |fuchsia.ui.focus.FocusChainListenerRegistry|
  void Register(
      fidl::InterfaceHandle<fuchsia::ui::focus::FocusChainListener> focus_chain_listener) override;

  const std::vector<zx_koid_t>& focus_chain() { return focus_chain_; }

  void RegisterViewRefFocused(zx_koid_t koid,
                              fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> vrf);

 private:
  // Ensure the focus chain is valid; preserve as much of the existing focus chain as possible.
  // - If the focus chain is still valid, do nothing.
  // - Otherwise, truncate the focus chain so that every pairwise parent-child relationship is valid
  //   in the current tree.
  // - If the entire focus chain is invalid, the new focus chain will contain only the new root.
  // - If the view tree is empty, the new focus chain is empty.
  void RepairFocus();

  // Transfers focus to |koid| and generates the new focus chain.
  //  - |koid| must be allowed to receive focus and must exist in the current view tree snapshot.
  //  - If the |snapshot_| is empty, then |koid| is allowed to be ZX_KOID_INVALID and will generate
  //    an empty focus_chain_.
  void SetFocus(zx_koid_t koid);

  // Replaces the focus chain with a new one. Additionally, if the new focus chain is different from
  // the old one:
  // - send new focus chain to all FocusChainListeners.
  // - send focus gained/lost to all ViewRefFocused-type listeners.
  void SetFocusChain(std::vector<zx_koid_t> update);

  // Dispatches the current focus chain to all registered listeners.
  void DispatchFocusChain() const;
  // Dispatches the current focus chain to |listener|.
  void DispatchFocusChainTo(const fuchsia::ui::focus::FocusChainListenerPtr& listener) const;

  // Dispatches focus events to view clients.
  void DispatchFocusEvents(zx_koid_t old_focus, zx_koid_t new_focus);

  fuchsia::ui::views::ViewRef CloneViewRefOf(zx_koid_t koid) const;
  fuchsia::ui::focus::FocusChain CloneFocusChain() const;

  std::vector<zx_koid_t> focus_chain_;

  std::shared_ptr<const view_tree::Snapshot> snapshot_ =
      std::make_shared<const view_tree::Snapshot>();

  fidl::BindingSet<fuchsia::ui::focus::FocusChainListenerRegistry> focus_chain_listener_registry_;
  uint64_t next_focus_chain_listener_id_ = 0;
  std::unordered_map<uint64_t, fuchsia::ui::focus::FocusChainListenerPtr> focus_chain_listeners_;

  // TODO(fxbug.dev/64376): Remove when we remove GFX input.
  const LegacyFocusListener legacy_focus_listener_;

  // Manages endpoints for fuchsia.ui.views.ViewRefFocused.
  ViewRefFocusedRegistry view_ref_focused_registry_;

  inspect::Node inspect_node_;
  inspect::LazyNode lazy_;
};

}  // namespace focus

#endif  // SRC_UI_SCENIC_LIB_FOCUS_FOCUS_MANAGER_H_
