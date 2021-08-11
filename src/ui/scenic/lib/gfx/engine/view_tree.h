// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_H_

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <zircon/types.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "src/ui/lib/escher/geometry/transform.h"
#include "src/ui/scenic/lib/gfx/engine/hit.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::gfx {

// Forward declaration to avoid circular include.
// TODO(fxbug.dev/59407): Disentangle the annotation logic from ViewTree.
class ViewHolder;

// Forward declaration to keep implementation at bottom of class.
struct ViewTreeNewRefNode;

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
// Invariants. Tree update operations and focus transfer operations are required to keep the maps,
// root, and focus chain in a valid state, where each parent pointer refers to a valid entry in the
// nodes_ map, the root is a valid entry in the nodes_ map, the focus chain is correctly updated
// [3], and each SessionId/RefNode pair has an entry in the ref_node_koids_ map.
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
    std::shared_ptr<fuchsia::ui::views::ViewRef> view_ref;

    // Focus events are generated and dispatched along this interface.
    EventReporterWeakPtr event_reporter;

    // Park a callback that returns whether a view may currently receive focus.
    fit::function<bool()> may_receive_focus;

    // Park a callback that returns whether a view may currently receive input.
    fit::function<bool()> is_input_suppressed;

    // Park a callback that returns the current global transform of the node.
    fit::function<glm::mat4()> global_transform;

    // Park a callback that returns the current bounding box of the node.
    fit::function<escher::BoundingBox()> bounding_box;

    // Park a callback that performs a hit test starting at this node.
    fit::function<void(const escher::ray4& world_space_ray, HitAccumulator<ViewHit>* accumulator,
                       bool semantic_hit_test)>
        hit_test;

    // Park a function that creates an annotation ViewHolder using given ViewHolderToken.
    // TODO(fxbug.dev/59407): Disentangle the annotation logic from ViewTree.
    fit::function<void(fxl::RefPtr<ViewHolder>)> add_annotation_view_holder;

    scheduling::SessionId session_id = 0u;  // Default value: an invalid ID.
  };

  // Return parent's KOID, if valid. Otherwise return std::nullopt.
  // Invariant: child exists in nodes_ map.
  std::optional<zx_koid_t> ParentOf(zx_koid_t child) const;

  // Return the scheduling::SessionId declared for a tracked node.
  // Always return 0u for AttachNode, otherwise return stored value for RefNode.
  // NOTE: Multiple KOIDs can return the same SessionId
  scheduling::SessionId SessionIdOf(zx_koid_t koid) const;

  // Return the event reporter declared for a tracked node.
  // Be forgiving: If koid is invalid, or is untracked, return a null event reporter.
  // Note that a valid and tracked koid may still return null, or later become null.
  EventReporterWeakPtr EventReporterOf(zx_koid_t koid) const;

  // Return the global transform of the node attached to a tracked |koid|.
  // Returns std::nullopt if no node was found or if the node is disconnected.
  std::optional<glm::mat4> GlobalTransformOf(zx_koid_t koid) const;

  // Performs a hit test starting from the node corresponding to |starting_view_koid|. The hit test
  // gathers all hits owned by the sub-tree rooted at |starting_view_koid|, in the clip volume
  // defined for |starting_view_koid|.
  // Hit tests act as if the node corresponding to |starting_ref_koid| is the root of the tree. This
  // means no clip bounds or other conditions are checked for ancestors of |starting_ref_koid|, but
  // all conditions are checked for the subtree, including |starting_ref_koid|.
  // If koid was found invalid the test returns with no hits.
  void HitTestFrom(zx_koid_t starting_view_koid, const escher::ray4& world_space_ray,
                   HitAccumulator<ViewHit>* accumulator, bool semantic_hit_test) const;

  // A session may have registered one or more RefNodes (typically just one).
  // Return the *connected* RefNode KOID associated with a particular SessionId.
  // Invariant: at most one RefNode KOID transitively connects to root_.
  // - This operation is O(N^2) in the number of RefNodes, but typically is linear in depth of tree.
  std::optional<zx_koid_t> ConnectedViewRefKoidOf(SessionId session_id) const;

  // Return true if koid is (1) valid and (2) exists in nodes_ map.
  bool IsTracked(zx_koid_t koid) const;

  // Given a node's KOID, return true if it transitively connects to node with |ancestor_koid| via
  // parent references.
  // Pre: |descendant_koid| is valid and exists in nodes_ map
  // Pre: |ancestor_koid| is valid and exists in nodes_ map
  // Invariant: each valid parent reference exists in nodes_ map
  // - This operation is O(N) in the depth of the view tree.
  bool IsDescendant(zx_koid_t descendant_koid, zx_koid_t ancestor_koid) const;

  // Given a node's KOID, return true if it transitively connects to root_.
  // Pre: koid exists in nodes_ map
  // Invariant: each valid parent reference exists in nodes_ map
  // - This operation is O(N) in the depth of the view tree.
  bool IsConnectedToScene(zx_koid_t koid) const;

  // "RTTI" for type validity.
  bool IsRefNode(zx_koid_t koid) const;

  // Return true if koid has "may receive focus" property set to true.
  // Returns true by default if not connected to the scene, since a disconnected view can never
  // receive focus anyway.
  // Pre: koid exists in nodes_ map
  // Pre: koid is a valid RefNode
  // NOTE: Scene connectivity is not required.
  bool MayReceiveFocus(zx_koid_t koid) const;

  // Return true if the koid or any of its ancestors has input suppressed
  // (hit_test_behavior set to kSuppress).
  // Pre: koid exists in nodes_ map
  // Pre: koid is a valid RefNode
  // NOTE: Scene connectivity is not required.
  bool IsInputSuppressed(zx_koid_t koid) const;

  // Try creating an annotation ViewHolder as the child of the View "koid" refers to.
  // Return the creation result enum.
  // TODO(fxbug.dev/59407): Disentangle the annotation logic from ViewTree.
  zx_status_t AddAnnotationViewHolder(zx_koid_t koid, fxl::RefPtr<ViewHolder> view_holder) const;

  // Debug-only check for state validity.  See "Invariants" section in class comment.
  // - Runtime is O(N^2), chiefly due to the "AttachNode, when a parent, has one child" check.
  bool IsStateValid() const;

  // Update tree topology.

  // Pre: view_ref is a valid ViewRef
  // Pre: view_ref not in nodes_ map
  // Pre: all callbacks are non-null
  void NewRefNode(ViewTreeNewRefNode new_node);

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

  // Invalidate the add_annotation_view_holder callback associated with koid.
  // Pre: koid exists in nodes_ map
  // Post: if koid is a valid RefNode, koid.add_annotation_view_holder is nullptr
  // NOTE: Scene connectivity is not required.
  // TODO(fxbug.dev/59407): Disentangle the annotation logic from ViewTree.
  void InvalidateAnnotationViewHolder(zx_koid_t koid);

  // Debug aid.
  // - Do not rely on the concrete format for testing or any other purpose.
  // - Do not rely on this function being runtime efficient; it is not guaranteed to be.
  std::string ToString() const;

  // Generate a snapshot of the ViewTree.
  // TODO(fxbug.dev/74533): The hit testing closures generated here are not thread safe.
  view_tree::SubtreeSnapshot Snapshot() const;

 private:
  // Map of ViewHolder's or ViewRef's KOID to its node representation.
  // - Nodes that are connected have an unbroken parent chain to root_.
  // - Nodes may be disconnected from root_ and still inhabit this map.
  // - Lifecycle (add/remove/connect/disconnect) is handled by callbacks from command processors.
  std::unordered_map<zx_koid_t, std::variant<AttachNode, RefNode>> nodes_;

  // The root of this ViewTree: a RefNode.
  zx_koid_t root_ = ZX_KOID_INVALID;

  // Multimap of Session ID to RefNode's KOID.
  // Each RefNode is tied to a particular Session ID. If a query against a particular Session ID
  // comes up empty, then that Session has not yet created a RefNode.
  // - This map must be kept in sync with the nodes_ map.
  // - Invariant: a map key must never be 0u - an invalid Session Id.
  // - Invariant: a map value's KOID must refer to a RefNode in the nodes_ map.
  // - Invariant: a KOID is the map value for at most one SessionId.
  // - Invariant: for each session, at most one RefNode is connected to root_.
  // - Lifecycle (add/remove) is handled by callbacks from command processors.
  std::unordered_multimap<SessionId, zx_koid_t> ref_node_koids_;
};

struct ViewTreeNewRefNode {
  fuchsia::ui::views::ViewRef view_ref;
  EventReporterWeakPtr event_reporter;
  fit::function<bool()> may_receive_focus;
  fit::function<bool()> is_input_suppressed;
  fit::function<glm::mat4()> global_transform;
  fit::function<escher::BoundingBox()> bounding_box;
  fit::function<void(const escher::ray4& world_space_ray, HitAccumulator<ViewHit>* accumulator,
                     bool semantic_hit_test)>
      hit_test;
  // TODO(fxbug.dev/59407): Disentangle the annotation logic from ViewTree.
  fit::function<void(fxl::RefPtr<ViewHolder>)> add_annotation_view_holder;
  scheduling::SessionId session_id = 0u;
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

// Handy aliases; suitable for client usage.
using ViewTreeUpdate =
    std::variant<ViewTreeNewRefNode, ViewTreeNewAttachNode, ViewTreeDeleteNode,
                 ViewTreeMakeGlobalRoot, ViewTreeConnectToParent, ViewTreeDisconnectFromParent>;
using ViewTreeUpdates = std::vector<ViewTreeUpdate>;

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_TREE_H_
