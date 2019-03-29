// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_stub.h"

#include <utility>

#include "garnet/bin/ui/view_manager/view_registry.h"
#include "garnet/bin/ui/view_manager/view_state.h"
#include "garnet/bin/ui/view_manager/view_tree_state.h"
#include "src/lib/fxl/logging.h"

namespace view_manager {

class PendingViewTransferState {
 public:
  PendingViewTransferState(std::unique_ptr<ViewStub> view_stub,
                           zx::eventpair transferred_view_token)
      : view_stub_(std::move(view_stub)),
        transferred_view_token_(std::move(transferred_view_token)) {}
  ~PendingViewTransferState() {}

  // A reference to keep the |ViewStub| alive until |OnViewResolved| is called.
  std::unique_ptr<ViewStub> view_stub_;

  // The token paired with the ViewHolder we want to transfer ownership to.
  zx::eventpair transferred_view_token_;
};

ViewStub::ViewStub(ViewRegistry* registry, zx::eventpair host_import_token)
    : registry_(registry),
      host_import_token_(std::move(host_import_token)),
      weak_factory_(this) {
  FXL_DCHECK(registry_);
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
}

ViewState* ViewStub::ReleaseView() {
  if (unavailable_)
    return nullptr;

  ViewState* state = state_;
  if (state) {
    FXL_DCHECK(state->view_stub() == this);
    state->set_view_stub(nullptr);
    state_ = nullptr;
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
}

void ViewStub::TransferViewWhenResolved(std::unique_ptr<ViewStub> view_stub,
                                        zx::eventpair transferred_view_token) {
  FXL_DCHECK(!container());  // Make sure we've been removed from the view tree
  FXL_DCHECK(!pending_view_transfer_);

  // When |OnViewResolved| gets called, we'll just transfer ownership
  // of the view instead of calling |ViewRegistry.OnViewResolved|.
  // Save the necessary state in |pending_view_transfer_|
  pending_view_transfer_.reset(new PendingViewTransferState(
      std::move(view_stub), std::move(transferred_view_token)));
}

void ViewStub::ReleaseHost() {
  host_import_token_.reset();
  host_node_.reset();
}

void ViewStub::ImportHostNode(scenic::Session* session) {
  // Should be dead code.
  FXL_CHECK(false);
}

}  // namespace view_manager
