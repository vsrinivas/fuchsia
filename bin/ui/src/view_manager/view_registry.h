// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_REGISTRY_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_REGISTRY_H_

#include <string>
#include <unordered_map>

#include "base/macros.h"
#include "mojo/services/gfx/composition/interfaces/compositor.mojom.h"
#include "mojo/services/ui/views/interfaces/view_associates.mojom.h"
#include "mojo/services/ui/views/interfaces/view_trees.mojom.h"
#include "mojo/services/ui/views/interfaces/views.mojom.h"
#include "services/ui/view_manager/view_associate_table.h"
#include "services/ui/view_manager/view_container_state.h"
#include "services/ui/view_manager/view_state.h"
#include "services/ui/view_manager/view_stub.h"
#include "services/ui/view_manager/view_tree_state.h"

namespace view_manager {

// Maintains a registry of the state of all views.
// All ViewState objects are owned by the registry.
class ViewRegistry : public mojo::ui::ViewInspector {
 public:
  using AssociateConnectionErrorCallback =
      ViewAssociateTable::AssociateConnectionErrorCallback;

  explicit ViewRegistry(mojo::gfx::composition::CompositorPtr compositor);
  ~ViewRegistry() override;

  // VIEW MANAGER REQUESTS

  // Creates a view and returns its ViewToken.
  void CreateView(
      mojo::InterfaceRequest<mojo::ui::View> view_request,
      mojo::InterfaceRequest<mojo::ui::ViewOwner> view_owner_request,
      mojo::ui::ViewListenerPtr view_listener,
      const mojo::String& label);

  // Creates a view tree.
  void CreateViewTree(
      mojo::InterfaceRequest<mojo::ui::ViewTree> view_tree_request,
      mojo::ui::ViewTreeListenerPtr view_tree_listener,
      const mojo::String& label);

  void RegisterViewAssociate(
      mojo::ui::ViewInspector* view_inspector,
      mojo::ui::ViewAssociatePtr view_associate,
      mojo::InterfaceRequest<mojo::ui::ViewAssociateOwner> view_associate_owner,
      const mojo::String& label);

  void FinishedRegisteringViewAssociates();

  // VIEW STUB REQUESTS

  void OnViewResolved(ViewStub* view_stub, mojo::ui::ViewTokenPtr view_token);
  void TransferViewOwner(mojo::ui::ViewTokenPtr view_token,
                         mojo::InterfaceRequest<mojo::ui::ViewOwner>
                             transferred_view_owner_request);

  // VIEW REQUESTS

  // Creates a scene for the view, replacing its current scene.
  // Destroys |view_state| if an error occurs.
  void CreateScene(ViewState* view_state,
                   mojo::InterfaceRequest<mojo::gfx::composition::Scene> scene);

  // Invalidates a view.
  // Destroys |view_state| if an error occurs.
  void Invalidate(ViewState* view_state);

  // Called when one of the view pipes is closed remotely.
  void OnViewDied(ViewState* view_state, const std::string& reason);

  // VIEW TREE REQUESTS

  // Sets the view tree's renderer.
  // Destroys |tree_state| if an error occurs.
  void SetRenderer(ViewTreeState* tree_state,
                   mojo::gfx::composition::RendererPtr renderer);

  // Called when one of the view tree pipes is closed remotely.
  void OnViewTreeDied(ViewTreeState* tree_state, const std::string& reason);

  // VIEW CONTAINER

  // Adds a child, reparenting it if necessary.
  // Destroys |container_state| if an error occurs.
  void AddChild(ViewContainerState* container_state,
                uint32_t child_key,
                mojo::InterfaceHandle<mojo::ui::ViewOwner> child_view_owner);

  // Removes a child.
  // Destroys |container_state| if an error occurs.
  void RemoveChild(ViewContainerState* container_state,
                   uint32_t child_key,
                   mojo::InterfaceRequest<mojo::ui::ViewOwner>
                       transferred_view_owner_request);

  // Sets a child's properties.
  // Destroys |container_state| if an error occurs.
  void SetChildProperties(ViewContainerState* container_state,
                          uint32_t child_key,
                          uint32_t child_scene_version,
                          mojo::ui::ViewPropertiesPtr child_properties);

  // Flushes changes to children.
  // Destroys |container_state| if an error occurs.
  void FlushChildren(ViewContainerState* container_state, uint32_t flush_token);

  // SERVICE PROVIDER REQUESTS

  // Connects to a view service.
  // Destroys |view_state| if an error occurs.
  void ConnectToViewService(ViewState* view_state,
                            const mojo::String& service_name,
                            mojo::ScopedMessagePipeHandle client_handle);

  // Connects to a view service.
  // Destroys |view_state| if an error occurs.
  void ConnectToViewTreeService(ViewTreeState* tree_state,
                                const mojo::String& service_name,
                                mojo::ScopedMessagePipeHandle client_handle);

  // VIEW INSPECTOR REQUESTS

  void GetHitTester(mojo::ui::ViewTreeTokenPtr view_tree_token,
                    mojo::InterfaceRequest<mojo::gfx::composition::HitTester>
                        hit_tester_request,
                    const GetHitTesterCallback& callback) override;

  void ResolveScenes(
      mojo::Array<mojo::gfx::composition::SceneTokenPtr> scene_tokens,
      const ResolveScenesCallback& callback) override;

 private:
  // LIFETIME

  void UnregisterView(ViewState* view_state);
  void UnregisterViewTree(ViewTreeState* tree_state);
  void UnregisterViewContainer(ViewContainerState* container_state);
  void UnregisterViewStub(std::unique_ptr<ViewStub> view_stub);
  void UnregisterChildren(ViewContainerState* container_state);

  // TREE MANIPULATION

  void AttachResolvedViewAndNotify(ViewStub* view_stub, ViewState* view_state);
  void ReleaseUnavailableViewAndNotify(ViewStub* view_stub);
  void HijackView(ViewState* view_state);
  void TransferOrUnregisterViewStub(std::unique_ptr<ViewStub> view_stub,
                                    mojo::InterfaceRequest<mojo::ui::ViewOwner>
                                        transferred_view_owner_request);

  // INVALIDATION

  void ScheduleViewInvalidation(ViewState* view_state, uint32_t flags);
  void ScheduleViewTreeInvalidation(ViewTreeState* tree_state, uint32_t flags);
  void TraverseViewTree(ViewTreeState* tree_state,
                        mojo::gfx::composition::FrameInfoPtr frame_info);
  void TraverseView(ViewState* view_state,
                    const mojo::gfx::composition::FrameInfo* frame_info,
                    bool parent_properties_changed);
  mojo::ui::ViewPropertiesPtr ResolveViewProperties(ViewState* view_state);

  // SCENE MANAGEMENT

  void OnViewSceneTokenAvailable(
      base::WeakPtr<ViewState> view_state_weak,
      mojo::gfx::composition::SceneTokenPtr scene_token);
  void OnStubSceneTokenAvailable(
      base::WeakPtr<ViewStub> view_stub_weak,
      mojo::gfx::composition::SceneTokenPtr scene_token);
  void PublishStubScene(ViewState* view_state);

  // RENDERING

  void SetRendererRootScene(ViewTreeState* view_tree);
  void OnRendererDied(ViewTreeState* view_tree);

  // SIGNALING

  void SendInvalidation(ViewState* view_state,
                        mojo::ui::ViewInvalidationPtr invalidation);

  void SendRendererDied(ViewTreeState* tree_state);

  void SendChildAttached(ViewContainerState* container_state,
                         uint32_t child_key,
                         mojo::ui::ViewInfoPtr child_view_info);
  void SendChildUnavailable(ViewContainerState* container_state,
                            uint32_t child_key);

  // LOOKUP

  ViewState* FindView(uint32_t view_token_value);
  ViewTreeState* FindViewTree(uint32_t view_tree_token_value);

  bool IsViewStateRegisteredDebug(ViewState* view_state) {
    return view_state && FindView(view_state->view_token()->value);
  }

  bool IsViewTreeStateRegisteredDebug(ViewTreeState* tree_state) {
    return tree_state && FindViewTree(tree_state->view_tree_token()->value);
  }

  bool IsViewContainerStateRegisteredDebug(
      ViewContainerState* container_state) {
    return container_state &&
           (IsViewStateRegisteredDebug(container_state->AsViewState()) ||
            IsViewTreeStateRegisteredDebug(container_state->AsViewTreeState()));
  }

  mojo::gfx::composition::CompositorPtr compositor_;
  ViewAssociateTable associate_table_;

  uint32_t next_view_token_value_ = 1u;
  uint32_t next_view_tree_token_value_ = 1u;
  std::unordered_map<uint32_t, ViewState*> views_by_token_;
  std::unordered_map<uint32_t, ViewState*> views_by_scene_token_;
  std::unordered_map<uint32_t, ViewTreeState*> view_trees_by_token_;

  DISALLOW_COPY_AND_ASSIGN(ViewRegistry);
};

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_REGISTRY_H_
