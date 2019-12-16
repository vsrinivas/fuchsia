// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_SERVICE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_SERVICE_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include <unordered_map>
#include <unordered_set>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/semantics/semantic_tree.h"
#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

class SemanticTreeService : public fuchsia::accessibility::semantics::SemanticTree {
 public:
  // Callback which will be used to notify that an error is encountered while trying to apply the
  // commit.
  using CloseChannelCallback = fit::function<void(zx_koid_t)>;

  SemanticTreeService(std::unique_ptr<::a11y::SemanticTree> tree,
                      fuchsia::ui::views::ViewRef view_ref,
                      fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
                      vfs::PseudoDir* debug_dir, CloseChannelCallback callback);

  ~SemanticTreeService() override;

  // Returns a weak pointer to the Semantic Tree owned by this service. Caller
  // must always check if the pointer is valid before accessing, as the pointer
  // may be invalidated. The pointer may become invalidated if the semantic
  // provider disconnects or if an error occurred. This is not thread safe. This
  // pointer may only be used in the same thread as this service is running.
  const fxl::WeakPtr<::a11y::SemanticTree> Get() const;

  // Returns the View Ref Koid of the semantics provider connected to this service.
  zx_koid_t view_ref_koid() const;

  // Calls OnSemanticsModeChanged() to notify semantic provider whether Semantics Manager is enabled
  // or not.
  // Also, deletes the semantic tree, when Semantics Manager is disabled.
  void EnableSemanticsUpdates(bool enabled);

  // |fuchsia::accessibility::semantics::SemanticsTree|
  void CommitUpdates(CommitUpdatesCallback callback) override;

  // |fuchsia::accessibility::semantics::SemanticsTree|
  void UpdateSemanticNodes(std::vector<fuchsia::accessibility::semantics::Node> nodes) override;

  // |fuchsia::accessibility::semantics::SemanticsTree|
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids) override;

 private:
  // Asks the semantics provider to perform an accessibility action on the
  // node with node id.
  void PerformAccessibilityAction(
      uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
          callback);

  // Asks the semantic provider to perform a hit test with given local
  // point.
  void PerformHitTesting(
      ::fuchsia::math::PointF local_point,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback);

  // Returns a string representation of the underlying |tree_| object.
  std::string LogSemanticTree();

  // Recursively traverses  |tree_| starting from |node|, filling |tree_log|
  // a string representation of the tree.
  void LogSemanticTreeHelper(const fuchsia::accessibility::semantics::Node* root_node,
                             int current_level, std::string* tree_log);

  // Function to create per view Log files under debug directory for
  // debugging semantic tree.
  void InitializeDebugEntry();

  // SignalHandler is called when ViewRef peer is destroyed. It is
  // responsible for closing the channel.
  void SignalHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal* signal);

  // The Semantic Tree data structure owned by this service. Semantic providers
  // typically modify the state of this tree via calls to Update(), Delete() and
  // Commit(), while semantic consumers only query the tree state via Get().
  // This tree is always in a valid state and rejects any tree update that
  // violates the tree structure.
  std::unique_ptr<::a11y::SemanticTree> tree_;

  // Holds pending updates to the tree that are not still commited.
  ::a11y::SemanticTree::TreeUpdates updates_;

  // Callback invoked to notify whoever holds the fidl channel to this service
  // to close it, effectively disconnecting the client and causing a reset of
  // this service.
  CloseChannelCallback close_channel_callback_;

  // Unique identifier of the view providing semantics .
  fuchsia::ui::views::ViewRef view_ref_;

  // handler of |view_ref_| received signals.
  async::WaitMethod<SemanticTreeService, &SemanticTreeService::SignalHandler> wait_;

  // Client-end channel of the fidl service to perform actions on the semantic provider.
  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener_;

  vfs::PseudoDir* const debug_dir_;
  bool semantics_manager_enabled_ = false;

  // Note: must be the last element on this class to ensure that it is the last to be destructed.
  std::unique_ptr<fxl::WeakPtrFactory<::a11y::SemanticTree>> semantic_tree_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(SemanticTreeService);
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTIC_TREE_SERVICE_H_
