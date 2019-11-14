// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_H_

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <zircon/types.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"

namespace scenic_impl::gfx {

// Represent the tree of ViewRefs in a scene graph, and maintain the global "focus chain".
//
// Types. A tree Node is either a RefNode or a AttachNode [1]. RefNode owns a
// fuchsia::ui::views::ViewRef for generating a focus chain. AttachNode represents the RefNode's
// parent in the scene graph. In GFX, these correspond to View and ViewHolder types; in 2D Layer,
// these correspond to Root and Link types.
//
// State. The main state is a map of Koid->Node, and each Node has a parent pointer of type Koid.
// The root of the tree is a RefNode, and its Koid is cached separately. The focus chain is a
// cached vector of Koid.
//
// Topology. Parent/child types alternate between RefNode and AttachNode. The tree root is a
// RefNode.  Each child points to its parent, but parents do not know their children. A RefNode may
// have many AttachNode children, but an AttachNode may have only 1 RefNode child. A subtree is
// typically (but not required to be) connected to the global root.
//
// Modifications. Each command processor (such as GFX or 2D Layer) must explicitly arrange node
// creation, node destruction, and node connectivity changes. Modifications directly mutate the
// global tree [2].
//
// Invariants. Tree update operations and focus transfer operations are required to keep the map,
// root, and focus chain in a valid state, where each parent pointer refers to a valid entry in the
// map, the root is a valid entry in the map, and the focus chain is correctly updated [3].
//
// Ownership. The global ViewTree instance is owned by SceneGraph.
//
// Event Dispatch. The tree, on explicit request, performs direct dispatch of necessary events, such
// as for fuchsia::ui::input::FocusEvent. Each node caches a weak pointer to its appropriate
// EventReporter. We assume that the EventReporter interface will grow to accommodate future needs.
//
// Remarks.
// [1] We don't need to explicitly represent the abstract Node type itself.
// [2] We *could* make the tree copyable for double buffering, but at the cost of extra complexity
//     and/or performance in managing ViewRef (eventpair) resources.
// [3] If performance is an issue, we could let the focus chain go stale, and repair it explicitly.
class ViewTree {
 public:
  // Represent a RefNode's parent, such as a ViewHolder in GFX, or a Link in 2D Layer.
  // Invariant: Child count must be 0 or 1.
  struct AttachNode {
    zx_koid_t parent = ZX_KOID_INVALID;
  };

  // Represent a "view" node of a ViewTree.
  // - May have multiple children.
  struct RefNode {
    zx_koid_t parent = ZX_KOID_INVALID;
    fuchsia::ui::views::ViewRef view_ref;

    // Focus events are generated and dispatched along this interface.
    EventReporterWeakPtr event_reporter;

    // Park a callback that returns whether a view may currently receive focus.
    fit::function<bool()> may_receive_focus;

    // TODO(24643): Remove this field.
    SessionId gfx_session_id = 0u;  // Default value: an invalid GFX session ID.
  };

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

  // Return the current focus chain with cloned ViewRefs.
  // - Error conditions should not force the return of an empty focus chain; instead, the root_, if
  //   valid, should be returned. This allows client-side recovery from focus loss.
  fuchsia::ui::focus::FocusChain CloneFocusChain() const;

  // Return the current focus chain.
  const std::vector<zx_koid_t>& focus_chain() const;

  // Return parent's KOID, if valid. Otherwise return std::nullopt.
  // Invariant: child exists in nodes_ map.
  std::optional<zx_koid_t> ParentOf(zx_koid_t child) const;

  // TODO(24643): Remove this method.
  // Return the GFX session ID declared for a tracked node.
  // Always return 0u for AttachNode, otherwise return stored value for RefNode.
  // NOTE: This is exclusively to provide legacy support for hard keyboard event dispatch.
  SessionId SessionIdOf(zx_koid_t koid) const;

  // Return the event reporter declared for a tracked node.
  // Be forgiving: If koid is invalid, or is untracked, return a null event reporter.
  // Note that a valid and tracked koid may still return null, or later become null.
  EventReporterWeakPtr EventReporterOf(zx_koid_t koid) const;

  // Return true if koid is (1) valid and (2) exists in nodes_ map.
  bool IsTracked(zx_koid_t koid) const;

  // Given a node's KOID, return true if it transitively connects to root_.
  // Pre: koid exists in nodes_ map
  // Invariant: each parent reference exists in nodes_ map
  // - This operation is O(N) in the depth of the view tree.
  bool IsConnected(zx_koid_t koid) const;

  // "RTTI" for type validity.
  bool IsRefNode(zx_koid_t koid) const;

  // Return true if koid has "may receive focus" property set to true.
  // Pre: koid exists in nodes_ map
  // Pre: koid is a valid RefNode
  // NOTE: Scene connectivity is not required.
  bool MayReceiveFocus(zx_koid_t koid) const;

  // Debug-only check for state validity.  See "Invariants" section in class comment.
  // - Runtime is O(N^2), chiefly due to the "AttachNode, when a parent, has one child" check.
  bool IsStateValid() const;

  // Request focus transfer to the proposed ViewRef's KOID. Return kAccept if successful.
  // - If the KOID is not in nodes_ map, or isn't a ViewRef, or isn't connected to the root, then
  //   return error.
  // - If the KOID is otherwise valid, but violates the focus transfer policy, then return error.
  FocusChangeStatus RequestFocusChange(zx_koid_t requestor, zx_koid_t request);

  // Update tree topology.

  // Pre: view_ref is a valid ViewRef
  // Pre: view_ref not in nodes_ map
  // Pre: may_receive_focus is a non-null callback
  void NewRefNode(fuchsia::ui::views::ViewRef view_ref, EventReporterWeakPtr reporter,
                  fit::function<bool()> may_receive_focus, SessionId gfx_session_id = 0u);

  // Pre: koid is a valid KOID
  // Pre: koid not in nodes_ map
  void NewAttachNode(zx_koid_t koid);

  // Pre: koid exists in nodes_ map
  // Post: each parent reference to koid set to ZX_KOID_INVALID
  // Post: if root_ is deleted, root_ set to ZX_KOID_INVALID
  void DeleteNode(zx_koid_t koid);

  // Pre: if valid, koid exists in nodes_map
  // Pre: if valid, koid is a valid RefNode
  // Pre: if valid, koid has "may receive focus" property
  // Post: root_ is set to koid
  // NOTE: koid can be ZX_KOID_INVALID, if the intent is to disconnect the entire tree.
  void MakeGlobalRoot(zx_koid_t koid);

  // Pre: child exists in nodes_ map
  // Pre: parent exists in nodes_ map
  // Invariant: child type != parent type
  void ConnectToParent(zx_koid_t child, zx_koid_t parent);

  // Pre child exists in nodes_ map
  // Pre: child.parent exists in nodes_ map
  // Post: child.parent set to ZX_KOID_INVALID
  void DisconnectFromParent(zx_koid_t child);

  // Debug aid.
  // - Do not rely on the concrete format for testing or any other purpose.
  // - Do not rely on this function being runtime efficient; it is not guaranteed to be.
  std::string ToString() const;

 private:
  // Utility.
  fuchsia::ui::views::ViewRef CloneViewRefOf(zx_koid_t koid) const;

  // Ensure the focus chain is valid; preserve as much of the existing focus chain as possible.
  // - If the focus chain is still valid, do nothing.
  // - Otherwise, "trim" the focus chain so that every pairwise parent-child relationship is valid
  //   in the current tree.
  // - Runtime is O(N) in the depth of the view tree, even for an already-valid focus chain.
  // - Mutator operations must call this function when finishing.
  // Post: if root_ is valid, (1) focus_chain_ is a prefix from the previous focus_chain_,
  //       (2) each element of focus_chain_ is a RefNode's KOID, and (3) each adjacent pair of
  //       KOIDs (P, R) is part of the ancestor hierarchy (P - Q - R) in the view tree.
  // Post: if root_ is invalid, focus_chain_ is empty.
  void RepairFocus();

  // Map of ViewHolder's or ViewRef's KOID to its node representation.
  // - Nodes that are connected have an unbroken parent chain to root_.
  // - Nodes may be disconnected from root_ and still inhabit this map.
  // - Lifecycle (add/remove/connect/disconnect) is handled by callbacks from command processors.
  std::unordered_map<zx_koid_t, std::variant<AttachNode, RefNode>> nodes_;

  // The root of this ViewTree: a RefNode.
  zx_koid_t root_ = ZX_KOID_INVALID;

  // The focus chain. The last element is the ViewRef considered to "have focus".
  // - Mutator operations are required to keep the focus chain updated.
  // - If no view has focus (because there is no root), then the focus chain is empty.
  std::vector<zx_koid_t> focus_chain_;
};

struct ViewTreeNewRefNode {
  fuchsia::ui::views::ViewRef view_ref;
  EventReporterWeakPtr event_reporter;
  fit::function<bool()> may_receive_focus;
  SessionId gfx_session_id = 0u;
};

struct ViewTreeNewAttachNode {
  zx_koid_t koid = ZX_KOID_INVALID;
};

struct ViewTreeDeleteNode {
  zx_koid_t koid = ZX_KOID_INVALID;
};

struct ViewTreeMakeGlobalRoot {
  zx_koid_t koid = ZX_KOID_INVALID;
};

struct ViewTreeConnectToParent {
  zx_koid_t child = ZX_KOID_INVALID;
  zx_koid_t parent = ZX_KOID_INVALID;
};

struct ViewTreeDisconnectFromParent {
  zx_koid_t koid = ZX_KOID_INVALID;
};

// Handy alias; suitable for client usage.
using ViewTreeUpdates = std::vector<
    std::variant<ViewTreeNewRefNode, ViewTreeNewAttachNode, ViewTreeDeleteNode,
                 ViewTreeMakeGlobalRoot, ViewTreeConnectToParent, ViewTreeDisconnectFromParent>>;

// Handy ViewRef-specific utility.
zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref);

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_H_
