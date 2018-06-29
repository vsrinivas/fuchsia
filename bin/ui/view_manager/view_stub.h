// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_STUB_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_STUB_H_

#include <memory>
#include <vector>

#include <lib/zx/eventpair.h>

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/ui/scenic/cpp/resources.h"

namespace view_manager {

class ViewContainerState;
class ViewRegistry;
class ViewState;
class ViewTreeState;
class PendingViewOwnerTransferState;

// Describes a link in the view hierarchy either from a parent view to one
// of its children or from the view tree to its root view.
//
// When this object is created, it is not yet known whether the linked
// view actually exists.  We must wait for a response from the view owner
// to resolve the view's token and associate the stub with its child.
//
// Instances of this object are held by a unique pointer owned by the
// parent view or view tree at the point where the view is being linked.
// Note that the lifetime of the views themselves is managed by the view
// registry.
//
// Note: sometimes, we might be waiting for |OnViewResolved| while this
// |ViewStub| has already been removed and ownership of the child is supposed
// to be transferred.
// In that case, this |ViewStub| holds a reference to itself and, when
// |OnViewResolved| is finally called, it tells the |view_registry| to
// immediately transfer ownership of the child view.
class ViewStub {
 public:
  // Begins the process of resolving a view.
  // Invokes |ViewRegistry.OnViewResolved| when the token is obtained
  // from the owner or passes nullptr if an error occurs.
  // |host_import_token| is the import token associated with the node
  // that the parent view exported to host the view's graphical contents.
  ViewStub(ViewRegistry* registry,
           fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner> owner,
           zx::eventpair host_import_token);
  ~ViewStub();

  fxl::WeakPtr<ViewStub> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Gets the view state referenced by the stub, or null if the view
  // has not yet been resolved or is unavailable.
  ViewState* state() const { return state_; }

  // Returns true if the view which was intended to be referenced by the
  // stub has become unavailable.
  bool is_unavailable() const { return unavailable_; }

  // Returns true if awaiting resolution of the view.
  bool is_pending() const { return !state_ && !unavailable_; }

  // Returns true if the view is linked into a tree or parent.
  bool is_linked() const { return tree_ || parent_; }

  // Returns true if the view is linked into a tree and has no parent.
  bool is_root_of_tree() const { return tree_ && !parent_; }

  // Gets the view tree to which this view belongs, or null if none.
  ViewTreeState* tree() const { return tree_; }

  // Gets the parent view state, or null if none.
  ViewState* parent() const { return parent_; }

  // Gets the container, or null if none.
  ViewContainerState* container() const;

  // Gets the key that this child has in its container, or 0 if none.
  uint32_t key() const { return key_; }

  // Gets the properties which the container set on this view, or null
  // if none set or the view has become unavailable.
  const ::fuchsia::ui::views_v1::ViewPropertiesPtr& properties() const { return properties_; }

  // Sets the properties set by the container.
  // May be called when the view is pending or attached but not after it
  // has become unavailable.
  void SetProperties(::fuchsia::ui::views_v1::ViewPropertiesPtr properties);

  // Binds the stub to the specified actual view, which must not be null.
  // Must be called at most once to apply the effects of resolving the
  // view owner.
  void AttachView(ViewState* state);

  // Marks the stub as unavailable.
  // Returns the previous view state, or null if none.
  ViewState* ReleaseView();

  // THESE METHODS SHOULD ONLY BE CALLED BY VIEW STATE OR VIEW TREE STATE

  // Sets the child's container and key.  Must not be null.
  void SetContainer(ViewContainerState* container, uint32_t key);

  // Resets the parent view state and tree pointers to null.
  void Unlink();

  // Called in the rare case when |OnViewResolved| hasn't been called, but
  // we have already been removed and the child view's ownership is supposed to
  // be transferred
  void TransferViewOwnerWhenViewResolved(
      std::unique_ptr<ViewStub> view_stub,
      fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
          transferred_view_owner_request);

  // Releases the host import token and host node.
  void ReleaseHost();

  // Creates the host node from the host import token.
  // This must be called by the view registry once it is time to bind the view's
  // graphical content to its host.  The host import token is consumed as
  // part of creating the host node.
  void ImportHostNode(scenic::Session* session);

  // Gets the imported host node, or null if none.
  scenic::ImportNode* host_node() { return host_node_.get(); }

 private:
  void SetTreeRecursively(ViewTreeState* tree);
  static void SetTreeForChildrenOfView(ViewState* view, ViewTreeState* tree);

  void OnViewResolved(::fuchsia::ui::views_v1_token::ViewToken view_token, bool success);

  // This is true when |ViewStub| has been transferred before |OnViewResolved|
  // has been called, and the child view's ownership is supposed to be
  // transferred. In that case, we will transfer ownership of the child
  // immediately once |OnViewResolved| is called.
  inline bool transfer_view_owner_when_view_resolved() const {
    return pending_view_owner_transfer_ != nullptr;
  }

  ViewRegistry* registry_;
  ::fuchsia::ui::views_v1_token::ViewOwnerPtr owner_;
  ViewState* state_ = nullptr;
  bool unavailable_ = false;

  zx::eventpair host_import_token_;
  std::unique_ptr<scenic::ImportNode> host_node_;

  // Non-null when we are waiting to transfer the |ViewOwner|.
  // Saves the |ViewOwner| we want to transfer ownership to, and a reference to
  // ourselves to keep us alive until |OnViewResolved| is called.
  std::unique_ptr<PendingViewOwnerTransferState> pending_view_owner_transfer_;

  ::fuchsia::ui::views_v1::ViewPropertiesPtr properties_;

  ViewTreeState* tree_ = nullptr;
  ViewState* parent_ = nullptr;
  uint32_t key_ = 0u;

  fxl::WeakPtrFactory<ViewStub> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewStub);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_STUB_H_
