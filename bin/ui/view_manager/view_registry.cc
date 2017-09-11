// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_registry.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "lib/app/cpp/connect.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/input/fidl/ime_service.fidl.h"
#include "lib/ui/views/cpp/formatting.h"
#include "garnet/bin/ui/view_manager/view_impl.h"
#include "garnet/bin/ui/view_manager/view_tree_impl.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/strings/string_printf.h"

namespace view_manager {
namespace {
// The height at which hit tests originate.
// TODO(MZ-163): This shouldn't be hardcoded here.
constexpr float kHitTestOriginZ = 10000.f;

bool Validate(const mozart::DisplayMetrics& value) {
  return std::isnormal(value.device_pixel_ratio) &&
         value.device_pixel_ratio > 0.f;
}

bool Validate(const mozart::ViewLayout& value) {
  return value.size && value.size->width >= 0 && value.size->height >= 0;
}

bool Validate(const mozart::ViewProperties& value) {
  if (value.display_metrics && !Validate(*value.display_metrics))
    return false;
  if (value.view_layout && !Validate(*value.view_layout))
    return false;
  return true;
}

// Returns true if the properties are valid and are sufficient for
// operating the view tree.
bool IsComplete(const mozart::ViewProperties& value) {
  return Validate(value) && value.view_layout && value.display_metrics;
}

void ApplyOverrides(mozart::ViewProperties* value,
                    const mozart::ViewProperties* overrides) {
  if (!overrides)
    return;
  if (overrides->display_metrics)
    value->display_metrics = overrides->display_metrics.Clone();
  if (overrides->view_layout)
    value->view_layout = overrides->view_layout.Clone();
}

std::string SanitizeLabel(const fidl::String& label) {
  return label.get().substr(0, mozart::ViewManager::kLabelMaxLength);
}

std::unique_ptr<FocusChain> CopyFocusChain(const FocusChain* chain) {
  std::unique_ptr<FocusChain> new_chain = nullptr;
  if (chain) {
    new_chain = std::make_unique<FocusChain>();
    new_chain->version = chain->version;
    new_chain->chain.resize(chain->chain.size());
    for (size_t index = 0; index < chain->chain.size(); ++index) {
      new_chain->chain[index] = chain->chain[index].Clone();
    }
  }
  return new_chain;
}

mozart::TransformPtr ToTransform(scenic::mat4Ptr matrix) {
  FXL_DCHECK(matrix);
  // Note: mat4 is column-major but transform is row-major
  auto transform = mozart::Transform::New();
  const auto& in = matrix->matrix;
  auto& out = transform->matrix;
  out.resize(16u);
  out[0] = in[0];
  out[1] = in[4];
  out[2] = in[8];
  out[3] = in[12];
  out[4] = in[1];
  out[5] = in[5];
  out[6] = in[9];
  out[7] = in[13];
  out[8] = in[2];
  out[9] = in[6];
  out[10] = in[10];
  out[11] = in[14];
  out[12] = in[3];
  out[13] = in[7];
  out[14] = in[11];
  out[15] = in[15];
  return transform;
}

}  // namespace

ViewRegistry::ViewRegistry(app::ApplicationContext* application_context)
    : application_context_(application_context),
      scene_manager_(application_context_
                         ->ConnectToEnvironmentService<scenic::SceneManager>()),
      session_(scene_manager_.get()),
      weak_factory_(this) {
  // TODO(MZ-128): Register session listener and destroy views if their
  // content nodes become unavailable.

  scene_manager_.set_connection_error_handler([] {
    FXL_LOG(ERROR) << "Exiting due to scene manager connection error.";
    exit(1);
  });

  session_.set_connection_error_handler([] {
    FXL_LOG(ERROR) << "Exiting due to session connection error.";
    exit(1);
  });
}

ViewRegistry::~ViewRegistry() {}

void ViewRegistry::GetSceneManager(
    fidl::InterfaceRequest<scenic::SceneManager> scene_manager_request) {
  // TODO(jeffbrown): We should have a better way to duplicate the
  // SceneManager connection without going back out through the environment.
  application_context_->ConnectToEnvironmentService(
      std::move(scene_manager_request));
}

// CREATE / DESTROY VIEWS

void ViewRegistry::CreateView(
    fidl::InterfaceRequest<mozart::View> view_request,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    mozart::ViewListenerPtr view_listener,
    mx::eventpair parent_export_token,
    const fidl::String& label) {
  FXL_DCHECK(view_request.is_pending());
  FXL_DCHECK(view_owner_request.is_pending());
  FXL_DCHECK(view_listener);
  FXL_DCHECK(parent_export_token);

  auto view_token = mozart::ViewToken::New();
  view_token->value = next_view_token_value_++;
  FXL_CHECK(view_token->value);
  FXL_CHECK(!FindView(view_token->value));

  // Create the state and bind the interfaces to it.
  ViewState* view_state =
      new ViewState(this, std::move(view_token), std::move(view_request),
                    std::move(view_listener), &session_, SanitizeLabel(label));
  view_state->BindOwner(std::move(view_owner_request));

  // Export a node which represents the view's attachment point.
  view_state->top_node().Export(std::move(parent_export_token));
  view_state->top_node().SetTag(view_state->view_token()->value);
  view_state->top_node().SetLabel(view_state->FormattedLabel());
  SchedulePresentSession();

  // Add to registry and return token.
  views_by_token_.emplace(view_state->view_token()->value, view_state);
  FXL_VLOG(1) << "CreateView: view=" << view_state;
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

  HijackView(view_state);
  UnregisterChildren(view_state);

  // Remove the view's content node from the session.
  view_state->top_node().Detach();
  SchedulePresentSession();

  // Remove from registry.
  views_by_token_.erase(view_state->view_token()->value);
  delete view_state;
}

// CREATE / DESTROY VIEW TREES

void ViewRegistry::CreateViewTree(
    fidl::InterfaceRequest<mozart::ViewTree> view_tree_request,
    mozart::ViewTreeListenerPtr view_tree_listener,
    const fidl::String& label) {
  FXL_DCHECK(view_tree_request.is_pending());
  FXL_DCHECK(view_tree_listener);

  auto view_tree_token = mozart::ViewTreeToken::New();
  view_tree_token->value = next_view_tree_token_value_++;
  FXL_CHECK(view_tree_token->value);
  FXL_CHECK(!FindViewTree(view_tree_token->value));

  // Create the state and bind the interfaces to it.
  ViewTreeState* tree_state = new ViewTreeState(
      this, std::move(view_tree_token), std::move(view_tree_request),
      std::move(view_tree_listener), SanitizeLabel(label));

  // Add to registry.
  view_trees_by_token_.emplace(tree_state->view_tree_token()->value,
                               tree_state);
  FXL_VLOG(1) << "CreateViewTree: tree=" << tree_state;
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
  view_trees_by_token_.erase(tree_state->view_tree_token()->value);
  delete tree_state;
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

  // Recursively unregister all children since they will become unowned
  // at this point taking care to unlink each one before its unregistration.
  for (auto& child : container_state->UnlinkAllChildren())
    UnregisterViewStub(std::move(child));
}

void ViewRegistry::ReleaseViewStubChildHost(ViewStub* view_stub) {
  view_stub->ReleaseHost();
  SchedulePresentSession();
}

// TREE MANIPULATION

void ViewRegistry::AddChild(
    ViewContainerState* container_state,
    uint32_t child_key,
    fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner,
    mx::eventpair host_import_token) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  FXL_DCHECK(child_view_owner);
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
  container_state->LinkChild(child_key, std::unique_ptr<ViewStub>(new ViewStub(
                                            this, std::move(child_view_owner),
                                            std::move(host_import_token))));
}

void ViewRegistry::RemoveChild(
    ViewContainerState* container_state,
    uint32_t child_key,
    fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
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

  // Unlink the child from its container.
  TransferOrUnregisterViewStub(container_state->UnlinkChild(child_key),
                               std::move(transferred_view_owner_request));
}

void ViewRegistry::SetChildProperties(
    ViewContainerState* container_state,
    uint32_t child_key,
    mozart::ViewPropertiesPtr child_properties) {
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

  // Immediately discard requests on unavailable views.
  ViewStub* child_stub = child_it->second.get();
  if (child_stub->is_unavailable())
    return;

  // Store the updated properties specified by the container if changed.
  if (child_properties.Equals(child_stub->properties()))
    return;

  // Apply the change.
  child_stub->SetProperties(std::move(child_properties));
  if (child_stub->state()) {
    InvalidateView(child_stub->state(),
                   ViewState::INVALIDATION_PROPERTIES_CHANGED);
  }
}

void ViewRegistry::RequestFocus(ViewContainerState* container_state,
                                uint32_t child_key) {
  FXL_DCHECK(IsViewContainerStateRegisteredDebug(container_state));
  FXL_VLOG(1) << "RequestFocus: container=" << container_state
              << ", child_key=" << child_key;

  // Check whether the child key exists in the container.
  auto child_it = container_state->children().find(child_key);
  if (child_it == container_state->children().end()) {
    FXL_LOG(ERROR) << "Attempted to modify child with an invalid key: "
                   << "container=" << container_state
                   << ", child_key=" << child_key;
    UnregisterViewContainer(container_state);
    return;
  }

  // Immediately discard requests on unavailable views.
  ViewStub* child_stub = child_it->second.get();
  if (child_stub->is_unavailable())
    return;

  // Set active focus chain for this view tree
  ViewTreeState* tree_state = child_stub->tree();
  tree_state->RequestFocus(child_stub);
}

void ViewRegistry::OnViewResolved(ViewStub* view_stub,
                                  mozart::ViewTokenPtr view_token) {
  FXL_DCHECK(view_stub);

  ViewState* view_state = view_token ? FindView(view_token->value) : nullptr;
  if (view_state)
    AttachResolvedViewAndNotify(view_stub, view_state);
  else
    ReleaseUnavailableViewAndNotify(view_stub);
}

void ViewRegistry::TransferViewOwner(
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
  FXL_DCHECK(view_token);
  FXL_DCHECK(transferred_view_owner_request.is_pending());

  ViewState* view_state = view_token ? FindView(view_token->value) : nullptr;
  if (view_state) {
    view_state->ReleaseOwner();  // don't need the ViewOwner pipe anymore
    view_state->BindOwner(std::move(transferred_view_owner_request));
  }
}

void ViewRegistry::AttachResolvedViewAndNotify(ViewStub* view_stub,
                                               ViewState* view_state) {
  FXL_DCHECK(view_stub);
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));
  FXL_VLOG(2) << "AttachViewStubAndNotify: view=" << view_state;

  // Hijack the view from its current container, if needed.
  HijackView(view_state);

  // Attach the view's content.
  if (view_stub->container()) {
    view_stub->ImportHostNode(&session_);
    view_stub->host_node()->AddChild(view_state->top_node());
    SchedulePresentSession();

    auto view_info = mozart::ViewInfo::New();
    SendChildAttached(view_stub->container(), view_stub->key(),
                      std::move(view_info));
  }

  // Attach the view.
  view_state->ReleaseOwner();  // don't need the ViewOwner pipe anymore
  view_stub->AttachView(view_state);
  InvalidateView(view_state, ViewState::INVALIDATION_PARENT_CHANGED);
}

void ViewRegistry::ReleaseUnavailableViewAndNotify(ViewStub* view_stub) {
  FXL_DCHECK(view_stub);
  FXL_VLOG(2) << "ReleaseUnavailableViewAndNotify: key=" << view_stub->key();

  ViewState* view_state = view_stub->ReleaseView();
  FXL_DCHECK(!view_state);

  if (view_stub->container())
    SendChildUnavailable(view_stub->container(), view_stub->key());
}

void ViewRegistry::HijackView(ViewState* view_state) {
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));

  ViewStub* view_stub = view_state->view_stub();
  if (view_stub) {
    view_stub->ReleaseView();
    if (view_stub->container())
      SendChildUnavailable(view_stub->container(), view_stub->key());
  }
}

void ViewRegistry::TransferOrUnregisterViewStub(
    std::unique_ptr<ViewStub> view_stub,
    fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
  FXL_DCHECK(view_stub);

  if (transferred_view_owner_request.is_pending()) {
    ReleaseViewStubChildHost(view_stub.get());

    if (view_stub->state()) {
      ViewState* view_state = view_stub->ReleaseView();
      InvalidateView(view_state, ViewState::INVALIDATION_PARENT_CHANGED);
      view_state->BindOwner(std::move(transferred_view_owner_request));
      return;
    }

    if (view_stub->is_pending()) {
      FXL_DCHECK(!view_stub->state());

      // Handle transfer of pending view.
      view_stub->TransferViewOwnerWhenViewResolved(
          std::move(view_stub), std::move(transferred_view_owner_request));

      return;
    }
  }
  UnregisterViewStub(std::move(view_stub));
}

// INVALIDATION

void ViewRegistry::InvalidateView(ViewState* view_state, uint32_t flags) {
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));
  FXL_VLOG(2) << "InvalidateView: view=" << view_state << ", flags=" << flags;

  view_state->set_invalidation_flags(view_state->invalidation_flags() | flags);
  if (view_state->view_stub() && view_state->view_stub()->tree()) {
    InvalidateViewTree(view_state->view_stub()->tree(),
                       ViewTreeState::INVALIDATION_VIEWS_INVALIDATED);
  }
}

void ViewRegistry::InvalidateViewTree(ViewTreeState* tree_state,
                                      uint32_t flags) {
  FXL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  FXL_VLOG(2) << "InvalidateViewTree: tree=" << tree_state
              << ", flags=" << flags;

  tree_state->set_invalidation_flags(tree_state->invalidation_flags() | flags);
  ScheduleTraversal();
}

void ViewRegistry::ScheduleTraversal() {
  if (!traversal_scheduled_) {
    traversal_scheduled_ = true;
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        [weak = weak_factory_.GetWeakPtr()] {
          if (weak)
            weak->Traverse();
        });
  }
}

void ViewRegistry::Traverse() {
  FXL_DCHECK(traversal_scheduled_);

  traversal_scheduled_ = false;
  for (const auto& pair : view_trees_by_token_)
    TraverseViewTree(pair.second);
}

void ViewRegistry::TraverseViewTree(ViewTreeState* tree_state) {
  FXL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  FXL_VLOG(2) << "TraverseViewTree: tree=" << tree_state
              << ", invalidation_flags=" << tree_state->invalidation_flags();

  uint32_t flags = tree_state->invalidation_flags();

  if (flags & ViewTreeState::INVALIDATION_VIEWS_INVALIDATED) {
    ViewStub* root_stub = tree_state->GetRoot();
    if (root_stub && root_stub->state())
      TraverseView(root_stub->state(), false);
  }

  tree_state->set_invalidation_flags(0u);
}

void ViewRegistry::TraverseView(ViewState* view_state,
                                bool parent_properties_changed) {
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));
  FXL_VLOG(2) << "TraverseView: view=" << view_state
              << ", parent_properties_changed=" << parent_properties_changed
              << ", invalidation_flags=" << view_state->invalidation_flags();

  uint32_t flags = view_state->invalidation_flags();

  // Update view properties.
  bool view_properties_changed = false;
  if (parent_properties_changed ||
      (flags & (ViewState::INVALIDATION_PROPERTIES_CHANGED |
                ViewState::INVALIDATION_PARENT_CHANGED))) {
    mozart::ViewPropertiesPtr properties = ResolveViewProperties(view_state);
    if (properties) {
      if (!view_state->issued_properties() ||
          !properties->Equals(*view_state->issued_properties())) {
        view_state->IssueProperties(std::move(properties));
        view_properties_changed = true;
      }
    }
    flags &= ~(ViewState::INVALIDATION_PROPERTIES_CHANGED |
               ViewState::INVALIDATION_PARENT_CHANGED);
  }

  // If we don't have view properties yet then we cannot pursue traversals
  // any further.
  if (!view_state->issued_properties()) {
    FXL_VLOG(2) << "View has no valid properties: view=" << view_state;
    view_state->set_invalidation_flags(flags);
    return;
  }

  // Deliver property change event if needed.
  bool send_properties = view_properties_changed ||
                         (flags & ViewState::INVALIDATION_RESEND_PROPERTIES);
  if (send_properties) {
    if (!(flags & ViewState::INVALIDATION_IN_PROGRESS)) {
      SendPropertiesChanged(view_state,
                            view_state->issued_properties().Clone());
      flags = ViewState::INVALIDATION_IN_PROGRESS;
    } else {
      FXL_VLOG(2) << "View invalidation stalled awaiting response: view="
                  << view_state;
      if (send_properties)
        flags |= ViewState::INVALIDATION_RESEND_PROPERTIES;
      flags |= ViewState::INVALIDATION_STALLED;
    }
  }
  view_state->set_invalidation_flags(flags);

  // TODO(jeffbrown): Optimize propagation.
  // This should defer traversal of the rest of the subtree until the view
  // flushes its container or a timeout expires.  We will need to be careful
  // to ensure that we completely process one traversal before starting the
  // next one and we'll have to retain some state.  The same behavior should
  // be applied when the parent's own properties change (assuming that it is
  // likely to want to resize its children, unless it says otherwise somehow).

  // Traverse all children.
  for (const auto& pair : view_state->children()) {
    ViewState* child_state = pair.second->state();
    if (child_state)
      TraverseView(pair.second->state(), view_properties_changed);
  }
}

mozart::ViewPropertiesPtr ViewRegistry::ResolveViewProperties(
    ViewState* view_state) {
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));

  ViewStub* view_stub = view_state->view_stub();
  if (!view_stub || !view_stub->properties())
    return nullptr;

  if (view_stub->parent()) {
    if (!view_stub->parent()->issued_properties())
      return nullptr;
    mozart::ViewPropertiesPtr properties =
        view_stub->parent()->issued_properties().Clone();
    ApplyOverrides(properties.get(), view_stub->properties().get());
    return properties;
  } else if (view_stub->is_root_of_tree()) {
    if (!view_stub->properties() || !IsComplete(*view_stub->properties())) {
      FXL_VLOG(2) << "View tree properties are incomplete: root=" << view_state
                  << ", properties=" << view_stub->properties();
      return nullptr;
    }
    return view_stub->properties().Clone();
  } else {
    return nullptr;
  }
}

void ViewRegistry::SchedulePresentSession() {
  if (!present_session_scheduled_) {
    present_session_scheduled_ = true;
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        [weak = weak_factory_.GetWeakPtr()] {
          if (weak)
            weak->PresentSession();
        });
  }
}

void ViewRegistry::PresentSession() {
  FXL_DCHECK(present_session_scheduled_);

  present_session_scheduled_ = false;
  session_.Present(0, [this](scenic::PresentationInfoPtr info) {});
}

// VIEW AND VIEW TREE SERVICE PROVIDERS

void ViewRegistry::ConnectToViewService(ViewState* view_state,
                                        const fidl::String& service_name,
                                        mx::channel client_handle) {
  FXL_DCHECK(IsViewStateRegisteredDebug(view_state));
  if (service_name == mozart::InputConnection::Name_) {
    CreateInputConnection(view_state->view_token()->Clone(),
                          fidl::InterfaceRequest<mozart::InputConnection>(
                              std::move(client_handle)));
  }
}

void ViewRegistry::ConnectToViewTreeService(ViewTreeState* tree_state,
                                            const fidl::String& service_name,
                                            mx::channel client_handle) {
  FXL_DCHECK(IsViewTreeStateRegisteredDebug(tree_state));
  if (service_name == mozart::InputDispatcher::Name_) {
    CreateInputDispatcher(tree_state->view_tree_token()->Clone(),
                          fidl::InterfaceRequest<mozart::InputDispatcher>(
                              std::move(client_handle)));
  }
}

// VIEW INSPECTOR

void ViewRegistry::HitTest(const mozart::ViewTreeToken& view_tree_token,
                           const mozart::PointF& point,
                           HitTestCallback callback) {
  FXL_VLOG(1) << "HitTest: tree=" << view_tree_token;

  ViewTreeState* view_tree = FindViewTree(view_tree_token.value);
  if (!view_tree || !view_tree->GetRoot() ||
      !view_tree->GetRoot()->host_node()) {
    callback(std::vector<ViewHit>());
    return;
  }

  // TODO(MZ-163): We're making 2D assumptions all over view manager.
  // We should redesign the relevant input related APIs to handle 3D content
  // and revisit this.
  session_.HitTest(
      view_tree->GetRoot()->host_node()->id(),
      (float[3]){point.x, point.y, kHitTestOriginZ}, (float[3]){0.f, 0.f, -1.f},
      [ this,
        callback = std::move(callback) ](fidl::Array<scenic::HitPtr> hits) {
        std::vector<ViewHit> view_hits;
        view_hits.reserve(hits.size());
        for (auto& hit : hits) {
          auto it = views_by_token_.find(hit->tag_value);
          if (it != views_by_token_.end()) {
            ViewState* view_state = it->second;
            view_hits.emplace_back(
                ViewHit{*view_state->view_token(),
                        ToTransform(std::move(hit->inverse_transform))});
          }
        }
        callback(std::move(view_hits));
      });
}

void ViewRegistry::ResolveFocusChain(
    mozart::ViewTreeTokenPtr view_tree_token,
    const ResolveFocusChainCallback& callback) {
  FXL_DCHECK(view_tree_token);
  FXL_VLOG(1) << "ResolveFocusChain: view_tree_token=" << view_tree_token;

  auto it = view_trees_by_token_.find(view_tree_token->value);
  if (it != view_trees_by_token_.end()) {
    callback(CopyFocusChain(it->second->focus_chain()));
  } else {
    callback(nullptr);
  }
}

void ViewRegistry::ActivateFocusChain(
    mozart::ViewTokenPtr view_token,
    const ActivateFocusChainCallback& callback) {
  FXL_DCHECK(view_token);
  FXL_VLOG(1) << "ActivateFocusChain: view_token=" << view_token;

  ViewState* view = FindView(view_token->value);
  if (!view) {
    callback(nullptr);
    return;
  }

  RequestFocus(view->view_stub()->container(), view->view_stub()->key());
  auto tree_state = view->view_stub()->tree();
  std::unique_ptr<FocusChain> new_chain =
      CopyFocusChain(tree_state->focus_chain());
  callback(std::move(new_chain));
}

void ViewRegistry::HasFocus(mozart::ViewTokenPtr view_token,
                            const HasFocusCallback& callback) {
  FXL_DCHECK(view_token);
  FXL_VLOG(1) << "HasFocus: view_token=" << view_token;
  ViewState* view = FindView(view_token->value);
  if (!view) {
    callback(false);
    return;
  }
  auto tree_state = view->view_stub()->tree();
  auto chain = tree_state->focus_chain();
  if (chain) {
    for (size_t index = 0; index < chain->chain.size(); ++index) {
      if (chain->chain[index]->value == view_token->value) {
        callback(true);
        return;
      }
    }
  }
  callback(false);
}

app::ServiceProvider* ViewRegistry::FindViewServiceProvider(
    uint32_t view_token,
    std::string service_name) {
  ViewState* view_state = FindView(view_token);
  if (!view_state) {
    return nullptr;
  }

  auto provider = view_state->GetServiceProviderIfSupports(service_name);
  while (!provider && view_state) {
    view_state = view_state->view_stub()->parent();
    provider = view_state
                   ? view_state->GetServiceProviderIfSupports(service_name)
                   : nullptr;
  }
  return provider;
}

void ViewRegistry::GetSoftKeyboardContainer(
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::SoftKeyboardContainer> container) {
  FXL_DCHECK(view_token);
  FXL_DCHECK(container.is_pending());
  FXL_VLOG(1) << "GetSoftKeyboardContainer: view_token=" << view_token;

  auto provider = FindViewServiceProvider(view_token->value,
                                          mozart::SoftKeyboardContainer::Name_);
  if (provider) {
    app::ConnectToService(provider, std::move(container));
  }
}

void ViewRegistry::GetImeService(
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::ImeService> ime_service) {
  FXL_DCHECK(view_token);
  FXL_DCHECK(ime_service.is_pending());
  FXL_VLOG(1) << "GetImeService: view_token=" << view_token;

  auto provider =
      FindViewServiceProvider(view_token->value, mozart::ImeService::Name_);
  if (provider) {
    app::ConnectToService(provider, std::move(ime_service));
  } else {
    application_context_->ConnectToEnvironmentService(std::move(ime_service));
  }
}

// EXTERNAL SIGNALING

void ViewRegistry::SendPropertiesChanged(ViewState* view_state,
                                         mozart::ViewPropertiesPtr properties) {
  FXL_DCHECK(view_state);
  FXL_DCHECK(view_state->view_listener());

  FXL_VLOG(1) << "SendPropertiesChanged: view_state=" << view_state
              << ", properties=" << properties;

  // It's safe to capture the view state because the ViewListener is closed
  // before the view state is destroyed so we will only receive the callback
  // if the view state is still alive.
  view_state->view_listener()->OnPropertiesChanged(
      std::move(properties), [this, view_state] {
        uint32_t old_flags = view_state->invalidation_flags();
        FXL_DCHECK(old_flags & ViewState::INVALIDATION_IN_PROGRESS);

        view_state->set_invalidation_flags(
            old_flags & ~(ViewState::INVALIDATION_IN_PROGRESS |
                          ViewState::INVALIDATION_STALLED));

        if (old_flags & ViewState::INVALIDATION_STALLED) {
          FXL_VLOG(2) << "View recovered from stalled invalidation: view_state="
                      << view_state;
          InvalidateView(view_state, 0u);
        }
      });
}

void ViewRegistry::SendChildAttached(ViewContainerState* container_state,
                                     uint32_t child_key,
                                     mozart::ViewInfoPtr child_view_info) {
  FXL_DCHECK(container_state);
  FXL_DCHECK(child_view_info);

  if (!container_state->view_container_listener())
    return;

  // TODO: Detect ANRs
  FXL_VLOG(1) << "SendChildAttached: container_state=" << container_state
              << ", child_key=" << child_key
              << ", child_view_info=" << child_view_info;
  container_state->view_container_listener()->OnChildAttached(
      child_key, std::move(child_view_info), [] {});
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

void ViewRegistry::DeliverEvent(const mozart::ViewToken* view_token,
                                mozart::InputEventPtr event,
                                ViewInspector::OnEventDelivered callback) {
  FXL_DCHECK(view_token);
  FXL_DCHECK(event);
  FXL_VLOG(1) << "DeliverEvent: view_token=" << *view_token
              << ", event=" << *event;

  auto it = input_connections_by_view_token_.find(view_token->value);
  if (it == input_connections_by_view_token_.end()) {
    FXL_VLOG(1)
        << "DeliverEvent: dropped because there was no input connection";
    if (callback)
      callback(false);
    return;
  }

  it->second->DeliverEvent(std::move(event), [callback](bool handled) {
    if (callback)
      callback(handled);
  });
}

void ViewRegistry::CreateInputConnection(
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::InputConnection> request) {
  FXL_DCHECK(view_token);
  FXL_DCHECK(request.is_pending());
  FXL_VLOG(1) << "CreateInputConnection: view_token=" << view_token;

  const uint32_t view_token_value = view_token->value;
  input_connections_by_view_token_.emplace(
      view_token_value,
      std::make_unique<InputConnectionImpl>(this, this, std::move(view_token),
                                            std::move(request)));
}

void ViewRegistry::OnInputConnectionDied(InputConnectionImpl* connection) {
  FXL_DCHECK(connection);
  auto it =
      input_connections_by_view_token_.find(connection->view_token()->value);
  FXL_DCHECK(it != input_connections_by_view_token_.end());
  FXL_DCHECK(it->second.get() == connection);
  FXL_VLOG(1) << "OnInputConnectionDied: view_token="
              << connection->view_token();

  input_connections_by_view_token_.erase(it);
}

void ViewRegistry::CreateInputDispatcher(
    mozart::ViewTreeTokenPtr view_tree_token,
    fidl::InterfaceRequest<mozart::InputDispatcher> request) {
  FXL_DCHECK(view_tree_token);
  FXL_DCHECK(request.is_pending());
  FXL_VLOG(1) << "CreateInputDispatcher: view_tree_token=" << view_tree_token;

  const uint32_t view_tree_token_value = view_tree_token->value;
  input_dispatchers_by_view_tree_token_.emplace(
      view_tree_token_value,
      std::unique_ptr<InputDispatcherImpl>(new InputDispatcherImpl(
          this, this, std::move(view_tree_token), std::move(request))));
}

void ViewRegistry::OnInputDispatcherDied(InputDispatcherImpl* dispatcher) {
  FXL_DCHECK(dispatcher);
  FXL_VLOG(1) << "OnInputDispatcherDied: view_tree_token="
              << dispatcher->view_tree_token();

  auto it = input_dispatchers_by_view_tree_token_.find(
      dispatcher->view_tree_token()->value);
  FXL_DCHECK(it != input_dispatchers_by_view_tree_token_.end());
  FXL_DCHECK(it->second.get() == dispatcher);

  input_dispatchers_by_view_tree_token_.erase(it);
}

// LOOKUP

ViewState* ViewRegistry::FindView(uint32_t view_token_value) {
  auto it = views_by_token_.find(view_token_value);
  return it != views_by_token_.end() ? it->second : nullptr;
}

ViewTreeState* ViewRegistry::FindViewTree(uint32_t view_tree_token_value) {
  auto it = view_trees_by_token_.find(view_tree_token_value);
  return it != view_trees_by_token_.end() ? it->second : nullptr;
}

}  // namespace view_manager
