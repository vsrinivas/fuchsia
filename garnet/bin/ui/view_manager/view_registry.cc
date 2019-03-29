// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_registry.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/bin/ui/view_manager/view_impl.h"
#include "garnet/bin/ui/view_manager/view_tree_impl.h"
#include "garnet/public/lib/escher/util/type_utils.h"
#include "lib/component/cpp/connect.h"
#include "lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/views/cpp/formatting.h"

namespace view_manager {
namespace {

class SnapshotCallbackImpl : public fuchsia::ui::gfx::SnapshotCallbackHACK {
 private:
  fit::function<void(::fuchsia::mem::Buffer)> callback_;
  fidl::Binding<::fuchsia::ui::gfx::SnapshotCallbackHACK> binding_;
  fit::function<void()> clear_fn_;

 public:
  explicit SnapshotCallbackImpl(
      fidl::InterfaceRequest<fuchsia::ui::gfx::SnapshotCallbackHACK> request,
      fit::function<void(::fuchsia::mem::Buffer)> callback)
      : callback_(std::move(callback)), binding_(this, std::move(request)) {}
  ~SnapshotCallbackImpl() {}
  void SetClear(fit::function<void()> clear_fn) {
    clear_fn_ = std::move(clear_fn);
  }

  virtual void OnData(::fuchsia::mem::Buffer data) override {
    callback_(std::move(data));
    if (clear_fn_)
      clear_fn_();
  }
};

bool Validate(const ::fuchsia::ui::viewsv1::ViewLayout& value) {
  return value.size.width >= 0 && value.size.height >= 0;
}

bool Validate(const ::fuchsia::ui::viewsv1::ViewProperties& value) {
  if (value.view_layout && !Validate(*value.view_layout))
    return false;
  return true;
}

std::string SanitizeLabel(fidl::StringPtr label) {
  return label.get().substr(0, ::fuchsia::ui::viewsv1::kLabelMaxLength);
}

}  // namespace

ViewRegistry::ViewRegistry(component::StartupContext* startup_context)
    : startup_context_(startup_context),
      scenic_(startup_context_
                  ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>()),
      session_(scenic_.get()),
      weak_factory_(this) {
  // TODO(MZ-128): Register session listener and destroy views if their
  // content nodes become unavailable.

  scenic_.set_error_handler([](zx_status_t error) {
    FXL_LOG(ERROR) << "Exiting due to scene manager connection error.";
    exit(1);
  });

  session_.set_error_handler([](zx_status_t error) {
    FXL_LOG(ERROR) << "Exiting due to session connection error.";
    exit(1);
  });
}

ViewRegistry::~ViewRegistry() {}

void ViewRegistry::GetScenic(
    fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> scenic_request) {
  // TODO(jeffbrown): We should have a better way to duplicate the
  // SceneManager connection without going back out through the environment.
  startup_context_->ConnectToEnvironmentService(std::move(scenic_request));
}

// CREATE / DESTROY VIEWS

void ViewRegistry::CreateView(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
    zx::eventpair view_token,
    ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener,
    zx::eventpair parent_export_token, fidl::StringPtr label) {
  FXL_DCHECK(view_request.is_valid());
  FXL_DCHECK(view_token);
  FXL_DCHECK(view_listener);
  FXL_DCHECK(parent_export_token);

  uint32_t view_id = next_view_id_value_++;
  FXL_CHECK(view_id);
  FXL_CHECK(!FindView(view_id));

  // Create the state.
  auto view_state = std::make_unique<ViewState>(
      this, view_id, std::move(view_request), std::move(view_listener),
      std::move(view_token), std::move(parent_export_token), scenic_.get(),
      SanitizeLabel(label));

  ViewState* view_state_ptr = view_state.get();
  views_by_token_.emplace(view_id, std::move(view_state));
  FXL_VLOG(1) << "CreateView: view=" << view_state_ptr;
}

void ViewRegistry::OnViewDied(ViewState* view_state,
                              const std::string& reason) {
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));
  FXL_VLOG(1) << "OnViewDied: view=" << view_state << ", reason=" << reason;

  UnregisterView(view_state);
}

void ViewRegistry::UnregisterView(ViewState* view_state) {
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));
  FXL_VLOG(1) << "UnregisterView: view=" << view_state;

  if (ViewStub* view_stub = view_state->view_stub()) {
    view_stub->ReleaseView();
  }
  UnregisterChildren(view_state);

  // Remove the view's content node from the session.
  view_state->ReleaseScenicResources();

  // Remove from registry.
  views_by_token_.erase(view_state->view_token());
}

// CREATE / DESTROY VIEW TREES

void ViewRegistry::CreateViewTree(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewTree> view_tree_request,
    ::fuchsia::ui::viewsv1::ViewTreeListenerPtr view_tree_listener,
    fidl::StringPtr label) {
  FXL_DCHECK(view_tree_request.is_valid());
  FXL_DCHECK(view_tree_listener);

  ::fuchsia::ui::viewsv1::ViewTreeToken view_tree_token;
  view_tree_token.value = next_view_tree_token_value_++;
  FXL_CHECK(view_tree_token.value);
  FXL_CHECK(!FindViewTree(view_tree_token.value));

  // Create the state and bind the interfaces to it.
  auto tree_state = std::make_unique<ViewTreeState>(
      this, view_tree_token, std::move(view_tree_request),
      std::move(view_tree_listener), scenic_.get(), SanitizeLabel(label));

  // Add to registry.
  ViewTreeState* tree_state_ptr = tree_state.get();
  view_trees_by_token_.emplace(tree_state->view_tree_token().value,
                               std::move(tree_state));
  FXL_VLOG(1) << "CreateViewTree: tree=" << tree_state_ptr;
}

void ViewRegistry::OnViewTreeDied(ViewTreeState* tree_state,
                                  const std::string& reason) {
  FXL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  FXL_VLOG(1) << "OnViewTreeDied: tree=" << tree_state << ", reason=" << reason;

  UnregisterViewTree(tree_state);
}

void ViewRegistry::UnregisterViewTree(ViewTreeState* tree_state) {
  FXL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  FXL_VLOG(1) << "UnregisterViewTree: tree=" << tree_state;

  UnregisterChildren(tree_state);

  // Remove from registry.
  view_trees_by_token_.erase(tree_state->view_tree_token().value);
}

// LIFETIME

void ViewRegistry::UnregisterViewContainer(
    ViewContainerState* container_state) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));

  ViewState* view_state = container_state->AsViewState();
  if (view_state)
    UnregisterView(view_state);
  else
    UnregisterViewTree(container_state->AsViewTreeState());
}

void ViewRegistry::UnregisterViewStub(std::unique_ptr<ViewStub> view_stub) {
  FXL_DCHECK(view_stub);

  ViewState* view_state = view_stub->ReleaseView();
  if (view_state)
    UnregisterView(view_state);

  ReleaseViewStubChildHost(view_stub.get());
}

void ViewRegistry::UnregisterChildren(ViewContainerState* container_state) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));

  container_state->RemoveAllChildren();
}

void ViewRegistry::ReleaseViewStubChildHost(ViewStub* view_stub) {
  view_stub->ReleaseHost();
  SchedulePresentSession();
}

// TREE MANIPULATION

void ViewRegistry::AddChild(ViewContainerState* container_state,
                            uint32_t child_key, zx::eventpair view_holder_token,
                            zx::eventpair host_import_token) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  FXL_DCHECK(view_holder_token);
  FXL_DCHECK(host_import_token);
  FXL_VLOG(1) << "AddChild: container=" << container_state
              << ", child_key=" << child_key;

  // Ensure there are no other children with the same key.
  if (container_state->children().find(child_key) !=
      container_state->children().end()) {
    FXL_LOG(ERROR) << "Attempted to add a child with a duplicate key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    return;
  }

  // If this is a view tree, ensure it only has one root.
  ViewTreeState* view_tree_state = container_state->AsViewTreeState();
  if (view_tree_state && !container_state->children().empty()) {
    FXL_LOG(ERROR) << "Attempted to add a second child to a view tree: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    return;
  }

  // Add a stub, pending resolution of the view owner.
  // Assuming the stub isn't removed prematurely, |OnViewResolved| will be
  // called asynchronously with the result of the resolution.
  container_state->AddChild(child_key, std::move(view_holder_token),
                            std::move(host_import_token));
}

void ViewRegistry::RemoveChild(ViewContainerState* container_state,
                               uint32_t child_key,
                               zx::eventpair transferred_view_holder_token) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  FXL_VLOG(1) << "RemoveChild: container=" << container_state
              << ", child_key=" << child_key;

  // Ensure the child key exists in the container.
  auto child_it = container_state->children().find(child_key);
  if (child_it == container_state->children().end()) {
    FXL_LOG(ERROR) << "Attempted to remove a child with an invalid key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    return;
  }

  container_state->RemoveChild(child_key,
                               std::move(transferred_view_holder_token));
}

void ViewRegistry::SetChildProperties(
    ViewContainerState* container_state, uint32_t child_key,
    ::fuchsia::ui::viewsv1::ViewPropertiesPtr child_properties) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  FXL_VLOG(1) << "SetChildProperties: container=" << container_state
              << ", child_key=" << child_key
              << ", child_properties=" << child_properties;

  // Check whether the properties are well-formed.
  if (child_properties && !Validate(*child_properties)) {
    FXL_LOG(ERROR) << "Attempted to set invalid child view properties: "
                   << "container=" << container_state
                   << ", child_key=" << child_key
                   << ", child_properties=" << child_properties;
    UnregisterViewContainer(container_state);
    return;
  }

  // Check whether the child key exists in the container.
  auto child_it = container_state->children().find(child_key);
  if (child_it == container_state->children().end()) {
    FXL_LOG(ERROR) << "Attempted to modify child with an invalid key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key
                   << ", child_properties=" << child_properties;
    UnregisterViewContainer(container_state);
    return;
  }

  container_state->SetChildProperties(child_key, std::move(child_properties));
}

void ViewRegistry::RequestSnapshotHACK(
    ViewContainerState* container_state, uint32_t child_key,
    fit::function<void(::fuchsia::mem::Buffer)> callback) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));

  // Check whether the child key exists in the container.
  auto child_it = container_state->children().find(child_key);
  if (child_it == container_state->children().end()) {
    FXL_LOG(ERROR) << "Attempted to modify child with an invalid key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    // TODO(SCN-978): Return an error to the caller for invalid data.
    callback(fuchsia::mem::Buffer{});
    return;
  }

  fuchsia::ui::gfx::SnapshotCallbackHACKPtr snapshot_callback;
  auto snapshot_callback_impl = std::make_shared<SnapshotCallbackImpl>(
      snapshot_callback.NewRequest(), std::move(callback));
  snapshot_callback_impl->SetClear([this, snapshot_callback_impl]() {
    snapshot_bindings_.remove(snapshot_callback_impl);
  });
  snapshot_bindings_.push_back(std::move(snapshot_callback_impl));

  // Snapshot the child.
  child_it->second->host_node->Snapshot(std::move(snapshot_callback));
  SchedulePresentSession();
}

void ViewRegistry::SendSizeChangeHintHACK(ViewContainerState* container_state,
                                          uint32_t child_key,
                                          float width_change_factor,
                                          float height_change_factor) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  FXL_VLOG(1) << "SendSizeChangeHintHACK: container=" << container_state
              << ", width_change_factor=" << width_change_factor
              << ", height_change_factor=" << height_change_factor << "}";

  // Check whether the child key exists in the container.
  auto child_it = container_state->children().find(child_key);
  if (child_it == container_state->children().end()) {
    FXL_LOG(ERROR) << "Attempted to modify child with an invalid key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    return;
  }

  child_it->second->host_node->SendSizeChangeHint(width_change_factor,
                                                  height_change_factor);
  SchedulePresentSession();
}

void ViewRegistry::SchedulePresentSession() {
  if (!present_session_scheduled_) {
    present_session_scheduled_ = true;
    async::PostTask(async_get_default_dispatcher(),
                    [weak = weak_factory_.GetWeakPtr()] {
                      if (weak)
                        weak->PresentSession();
                    });
  }
}

void ViewRegistry::PresentSession() {
  FXL_DCHECK(present_session_scheduled_);

  present_session_scheduled_ = false;
  session_.Present(0, [this](fuchsia::images::PresentationInfo info) {});
}

// VIEW AND VIEW TREE SERVICE PROVIDERS

void ViewRegistry::ConnectToViewService(ViewState* view_state,
                                        const fidl::StringPtr& service_name,
                                        zx::channel client_handle) {
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));
}

void ViewRegistry::ConnectToViewTreeService(ViewTreeState* tree_state,
                                            const fidl::StringPtr& service_name,
                                            zx::channel client_handle) {
  FXL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
}

// EXTERNAL SIGNALING

void ViewRegistry::SendChildAttached(
    ViewContainerState* container_state, uint32_t child_key,
    ::fuchsia::ui::viewsv1::ViewInfo child_view_info) {
  FXL_DCHECK(container_state);

  if (!container_state->view_container_listener())
    return;

  // TODO: Detect ANRs
  FXL_VLOG(1) << "SendChildAttached: container_state=" << container_state
              << ", child_key=" << child_key
              << ", child_view_info=" << child_view_info;
  container_state->view_container_listener()->OnChildAttached(
      child_key, child_view_info, [] {});
}

void ViewRegistry::SendChildUnavailable(ViewContainerState* container_state,
                                        uint32_t child_key) {
  FXL_DCHECK(container_state);

  if (!container_state->view_container_listener())
    return;

  // TODO: Detect ANRs
  FXL_VLOG(1) << "SendChildUnavailable: container=" << container_state
              << ", child_key=" << child_key;
  container_state->view_container_listener()->OnChildUnavailable(child_key,
                                                                 [] {});
}

// TRANSFERRING VIEWS

std::unique_ptr<ViewContainerState::ChildView> ViewRegistry::FindOrphanedView(
    zx_handle_t view_holder_token) {
  zx_koid_t peer_koid = fsl::GetRelatedKoid(view_holder_token);
  auto view_it = orphaned_views_.find(peer_koid);
  if (view_it != orphaned_views_.end()) {
    auto child_view = std::move(view_it->second.child_view);
    orphaned_views_.erase(view_it);
    return child_view;
  }
  return nullptr;
}
void ViewRegistry::AddOrphanedView(
    zx::eventpair view_holder_token,
    std::unique_ptr<ViewContainerState::ChildView> child) {
  zx_koid_t koid = fsl::GetKoid(view_holder_token.get());
  orphaned_views_[koid] = {std::move(view_holder_token), std::move(child)};
}

void ViewRegistry::RemoveOrphanedView(ViewContainerState::ChildView* child) {
  for (auto entry_it = orphaned_views_.begin();
       entry_it != orphaned_views_.end(); entry_it++) {
    if (entry_it->second.child_view.get() == child) {
      orphaned_views_.erase(entry_it);
    }
  }
}

// SNAPSHOT
// TODO(SCN-1263): Get Snapshots working with Views v2
void ViewRegistry::TakeSnapshot(
    uint64_t view_koid, fit::function<void(::fuchsia::mem::Buffer)> callback) {}

// LOOKUP

ViewState* ViewRegistry::FindView(uint32_t view_token) {
  auto it = views_by_token_.find(view_token);
  return it != views_by_token_.end() ? it->second.get() : nullptr;
}

ViewTreeState* ViewRegistry::FindViewTree(uint32_t view_tree_token_value) {
  auto it = view_trees_by_token_.find(view_tree_token_value);
  return it != view_trees_by_token_.end() ? it->second.get() : nullptr;
}

}  // namespace view_manager
