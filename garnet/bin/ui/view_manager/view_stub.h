// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_STUB_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_STUB_H_

#include <memory>
#include <vector>

#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "lib/ui/scenic/cpp/resources.h"

namespace view_manager {

class ViewContainerState;
class ViewRegistry;
class ViewState;
class ViewStub;
class ViewTreeState;
class PendingViewTransferState;
using ViewLinker = scenic_impl::gfx::ObjectLinker<ViewStub, ViewState>;

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
  // |host_import_token| is the import token for the node exported by the parent
  // view in order to host this view's graphical contents.
  ViewStub(ViewRegistry* registry, zx::eventpair host_import_token);
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
  const ::fuchsia::ui::viewsv1::ViewPropertiesPtr& properties() const {
    return properties_;
  }

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

  // Called in the rare case when |OnViewResolved| hasn't been called, but
  // we have already been removed and the child view's ownership is supposed to
  // be transferred
  void TransferViewWhenResolved(std::unique_ptr<ViewStub> view_stub,
                                zx::eventpair transferred_view_token);

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
  // This is true when |ViewStub| has been transferred before |OnViewResolved|
  // has been called, and the child view's ownership is supposed to be
  // transferred. In that case, we will transfer ownership of the child
  // immediately once |OnViewResolved| is called.
  inline bool transfer_view_when_resolved() const {
    return pending_view_transfer_ != nullptr;
  }

  ViewRegistry* registry_;
  ViewState* state_ = nullptr;
  bool unavailable_ = false;

  zx::eventpair host_import_token_;
  std::unique_ptr<scenic::ImportNode> host_node_;

  // Non-null when we are waiting to transfer the view.
  //
  // Saves the ViewHolder token we want to transfer ownership to, and a
  // reference to ourselves to keep us alive until |OnViewResolved| is called.
  std::unique_ptr<PendingViewTransferState> pending_view_transfer_;

  ::fuchsia::ui::viewsv1::ViewPropertiesPtr properties_;

  ViewTreeState* tree_ = nullptr;
  ViewState* parent_ = nullptr;
  uint32_t key_ = 0u;

  fxl::WeakPtrFactory<ViewStub> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewStub);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_STUB_H_
