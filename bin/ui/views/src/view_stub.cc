// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/view_manager/view_stub.h"

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "services/ui/view_manager/view_registry.h"
#include "services/ui/view_manager/view_state.h"
#include "services/ui/view_manager/view_tree_state.h"

namespace view_manager {

class PendingViewOwnerTransferState {
 public:
  PendingViewOwnerTransferState(std::unique_ptr<ViewStub> view_stub,
                                mojo::InterfaceRequest<mojo::ui::ViewOwner>
                                    transferred_view_owner_request)
      : view_stub_(std::move(view_stub)),
        transferred_view_owner_request_(transferred_view_owner_request.Pass()) {
  }

  ~PendingViewOwnerTransferState() {}

  // A reference to keep the |ViewStub| alive until |OnViewResolved| is called.
  std::unique_ptr<ViewStub> view_stub_;

  // The |ViewOwner| we want to transfer ownership to.
  mojo::InterfaceRequest<mojo::ui::ViewOwner> transferred_view_owner_request_;
};

ViewStub::ViewStub(ViewRegistry* registry,
                   mojo::InterfaceHandle<mojo::ui::ViewOwner> owner)
    : registry_(registry),
      owner_(mojo::ui::ViewOwnerPtr::Create(std::move(owner))),
      weak_factory_(this) {
  DCHECK(registry_);
  DCHECK(owner_);

  owner_.set_connection_error_handler(
      base::Bind(&ViewStub::OnViewResolved, base::Unretained(this), nullptr));
  owner_->GetToken(
      base::Bind(&ViewStub::OnViewResolved, base::Unretained(this)));
}

ViewStub::~ViewStub() {
  // Ensure that everything was properly released before this object was
  // destroyed.  The |ViewRegistry| is responsible for maintaining the
  // invariant that all |ViewState| objects are owned so by the time we
  // get here, the view should have found a new owner or been unregistered.
  DCHECK(is_unavailable());
}

ViewContainerState* ViewStub::container() const {
  return parent_ ? static_cast<ViewContainerState*>(parent_) : tree_;
}

void ViewStub::AttachView(ViewState* state,
                          mojo::gfx::composition::ScenePtr stub_scene) {
  DCHECK(state);
  DCHECK(!state->view_stub());
  DCHECK(stub_scene);
  DCHECK(is_pending());

  state_ = state;
  state_->set_view_stub(this);
  stub_scene_ = stub_scene.Pass();
  SetTreeForChildrenOfView(state_, tree_);
}

void ViewStub::SetProperties(uint32_t scene_version,
                             mojo::ui::ViewPropertiesPtr properties) {
  DCHECK(!is_unavailable());

  scene_version_ = scene_version;
  properties_ = properties.Pass();
}

void ViewStub::SetStubSceneToken(
    mojo::gfx::composition::SceneTokenPtr stub_scene_token) {
  DCHECK(stub_scene_token);
  DCHECK(state_);
  DCHECK(stub_scene_);
  DCHECK(!stub_scene_token_);

  stub_scene_token_ = stub_scene_token.Pass();
}

ViewState* ViewStub::ReleaseView() {
  if (is_unavailable())
    return nullptr;

  ViewState* state = state_;
  if (state) {
    DCHECK(state->view_stub() == this);
    state->set_view_stub(nullptr);
    state_ = nullptr;
    stub_scene_.reset();
    stub_scene_token_.reset();
    SetTreeForChildrenOfView(state, nullptr);
  }
  scene_version_ = mojo::gfx::composition::kSceneVersionNone;
  properties_.reset();
  unavailable_ = true;
  return state;
}

void ViewStub::SetContainer(ViewContainerState* container, uint32_t key) {
  DCHECK(container);
  DCHECK(!tree_ && !parent_);

  key_ = key;
  parent_ = container->AsViewState();
  if (parent_) {
    if (parent_->view_stub())
      SetTreeRecursively(parent_->view_stub()->tree());
  } else {
    ViewTreeState* tree = container->AsViewTreeState();
    DCHECK(tree);
    SetTreeRecursively(tree);
  }
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

void ViewStub::OnViewResolved(mojo::ui::ViewTokenPtr view_token) {
  if (transfer_view_owner_when_view_resolved()) {
    DCHECK(!container());  // Make sure we're removed from the view tree
    DCHECK(pending_view_owner_transfer_->view_stub_ != nullptr);
    // TODO(mikejurka): any other way to check that
    // transferred_view_owner_request_ is not null?
    DCHECK(pending_view_owner_transfer_->transferred_view_owner_request_
               .is_pending());
    DCHECK(owner_);
    owner_.reset();

    registry_->TransferViewOwner(
        view_token.Pass(),
        pending_view_owner_transfer_->transferred_view_owner_request_.Pass());

    // We don't have any |view_state| resolved to us now, but |ReleaseView| will
    // still mark us as unavailable and clear properties
    ReleaseView();

    // |pending_view_owner_transfer_| holds a reference to ourselves. Don't hold
    // that reference anymore, which should release us immediately.
    pending_view_owner_transfer_.reset();
  } else {
    DCHECK(owner_);
    owner_.reset();
    registry_->OnViewResolved(this, view_token.Pass());
  }
}

void ViewStub::TransferViewOwnerWhenViewResolved(
    std::unique_ptr<ViewStub> view_stub,
    mojo::InterfaceRequest<mojo::ui::ViewOwner>
        transferred_view_owner_request) {
  DCHECK(!container());  // Make sure we've been removed from the view tree
  DCHECK(!pending_view_owner_transfer_);

  // When |OnViewResolved| gets called, we'll just transfer ownership
  // of the view instead of calling |ViewRegistry.OnViewResolved|.
  // Save the necessary state in |pending_view_owner_transfer_|
  pending_view_owner_transfer_.reset(new PendingViewOwnerTransferState(
      std::move(view_stub), transferred_view_owner_request.Pass()));

  // TODO(mikejurka): should we have an error handler on
  // transferred_view_owner_request_?
}

}  // namespace view_manager
