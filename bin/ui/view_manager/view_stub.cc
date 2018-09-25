// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_stub.h"

#include <utility>

#include "garnet/bin/ui/view_manager/view_registry.h"
#include "garnet/bin/ui/view_manager/view_state.h"
#include "garnet/bin/ui/view_manager/view_tree_state.h"
#include "lib/fxl/logging.h"

namespace view_manager {

class PendingViewOwnerTransferState {
 public:
  PendingViewOwnerTransferState(
      std::unique_ptr<ViewStub> view_stub,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          transferred_view_owner_request)
      : view_stub_(std::move(view_stub)),
        transferred_view_owner_request_(
            std::move(transferred_view_owner_request)) {}

  ~PendingViewOwnerTransferState() {}

  // A reference to keep the |ViewStub| alive until |OnViewResolved| is called.
  std::unique_ptr<ViewStub> view_stub_;

  // The |ViewOwner| we want to transfer ownership to.
  fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
      transferred_view_owner_request_;
};

ViewStub::ViewStub(ViewRegistry* registry, View1Linker::ExportLink view_link,
                   zx::eventpair host_import_token)
    : registry_(registry),
      view_link_(std::move(view_link)),
      host_import_token_(std::move(host_import_token)),
      weak_factory_(this) {
  FXL_DCHECK(registry_);
  FXL_DCHECK(view_link_.valid());
  FXL_DCHECK(host_import_token_);
}

ViewStub::~ViewStub() {
  // Ensure that everything was properly released before this object was
  // destroyed.  The |ViewRegistry| is responsible for maintaining the
  // invariant that all |ViewState| objects are owned so by the time we
  // get here, the view should have found a new owner or been unregistered.
  FXL_DCHECK(is_unavailable());
}

ViewContainerState* ViewStub::container() const {
  return parent_ ? static_cast<ViewContainerState*>(parent_) : tree_;
}

void ViewStub::AttachView(ViewState* state) {
  FXL_DCHECK(state);
  FXL_DCHECK(!state->view_stub());
  FXL_DCHECK(is_pending());

  state_ = state;
  state_->set_view_stub(this);
  SetTreeForChildrenOfView(state_, tree_);
}

void ViewStub::SetProperties(
    ::fuchsia::ui::viewsv1::ViewPropertiesPtr properties) {
  FXL_DCHECK(!is_unavailable());

  properties_ = std::move(properties);
}

ViewState* ViewStub::ReleaseView() {
  if (unavailable_)
    return nullptr;

  ViewState* state = state_;
  if (state) {
    FXL_DCHECK(state->view_stub() == this);
    state->set_view_stub(nullptr);
    state_ = nullptr;
    SetTreeForChildrenOfView(state, nullptr);
  }
  properties_.reset();
  unavailable_ = true;
  return state;
}

void ViewStub::SetContainer(ViewContainerState* container, uint32_t key) {
  FXL_DCHECK(container);
  FXL_DCHECK(!tree_ && !parent_);

  key_ = key;
  parent_ = container->AsViewState();
  if (parent_) {
    if (parent_->view_stub())
      SetTreeRecursively(parent_->view_stub()->tree());
  } else {
    ViewTreeState* tree = container->AsViewTreeState();
    FXL_DCHECK(tree);
    SetTreeRecursively(tree);
  }

  // We cannot call this in the constructor, because this might resolve
  // immediately.  The resolution callback assumes that |container| has been
  // set.
  // TODO(SCN-972): Remove the need for this by making callbacks fire async.
  view_link_.Initialize(this,
                        [this](ViewState* state) {
                          FXL_VLOG(1) << "ViewStub connected: " << this;
                          OnViewResolved(state, true);
                        },
                        [this] {
                          FXL_VLOG(1) << "ViewStub disconnected: " << this;
                          OnViewResolved(nullptr, false);
                        });
}

void ViewStub::Unlink() {
  parent_ = nullptr;
  key_ = 0;
  SetTreeRecursively(nullptr);
}

void ViewStub::SetTreeRecursively(ViewTreeState* tree) {
  if (tree_ == tree)
    return;
  tree_ = tree;
  if (state_)
    SetTreeForChildrenOfView(state_, tree);
}

void ViewStub::SetTreeForChildrenOfView(ViewState* view, ViewTreeState* tree) {
  for (const auto& pair : view->children()) {
    pair.second->SetTreeRecursively(tree);
  }
}

void ViewStub::OnViewResolved(ViewState* view_state, bool success) {
  if (success && transfer_view_owner_when_view_resolved()) {
    // While we were waiting for linking, the view was transferred to a new
    // ViewOwner. Now that we are linked, transfer the ownership
    // correctly internally.
    FXL_DCHECK(!container());  // Make sure we're removed from the view tree
    FXL_DCHECK(pending_view_owner_transfer_->view_stub_ != nullptr);
    FXL_DCHECK(pending_view_owner_transfer_->transferred_view_owner_request_
                   .is_valid());

    registry_->TransferViewOwner(
        view_state,
        std::move(
            pending_view_owner_transfer_->transferred_view_owner_request_));

    // We don't have any |view_state| resolved to us now, but |ReleaseView| will
    // still mark us as unavailable and clear properties
    ReleaseView();

    // |pending_view_owner_transfer_| holds a reference to ourselves. Don't hold
    // that reference anymore, which should release us immediately.
    pending_view_owner_transfer_.reset();
  } else {
    // 1. We got the linking callback as expected (in which case view_state is
    // non-null and success is true).
    // 2. Or, the ViewOwner was closed before linking (in which case view_state
    // is null and success if false).
    registry_->OnViewResolved(this, view_state);
  }
}

void ViewStub::TransferViewOwnerWhenViewResolved(
    std::unique_ptr<ViewStub> view_stub,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        transferred_view_owner_request) {
  FXL_DCHECK(!container());  // Make sure we've been removed from the view tree
  FXL_DCHECK(!pending_view_owner_transfer_);

  // When |OnViewResolved| gets called, we'll just transfer ownership
  // of the view instead of calling |ViewRegistry.OnViewResolved|.
  // Save the necessary state in |pending_view_owner_transfer_|
  pending_view_owner_transfer_.reset(new PendingViewOwnerTransferState(
      std::move(view_stub), std::move(transferred_view_owner_request)));

  // TODO(mikejurka): should we have an error handler on
  // transferred_view_owner_request_?
}

void ViewStub::ReleaseHost() {
  host_import_token_.reset();
  host_node_.reset();
}

void ViewStub::ImportHostNode(scenic::Session* session) {
  FXL_DCHECK(host_import_token_);
  FXL_DCHECK(!host_node_);

  host_node_.reset(new scenic::ImportNode(session));
  host_node_->Bind(std::move(host_import_token_));
}

}  // namespace view_manager
