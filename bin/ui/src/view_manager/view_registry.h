// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_VIEW_REGISTRY_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_VIEW_REGISTRY_H_

#include <string>
#include <unordered_map>

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/views/view_trees.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "apps/mozart/src/view_manager/input/input_connection_impl.h"
#include "apps/mozart/src/view_manager/input/input_dispatcher_impl.h"
#include "apps/mozart/src/view_manager/internal/input_owner.h"
#include "apps/mozart/src/view_manager/internal/view_inspector.h"
#include "apps/mozart/src/view_manager/view_container_state.h"
#include "apps/mozart/src/view_manager/view_state.h"
#include "apps/mozart/src/view_manager/view_stub.h"
#include "apps/mozart/src/view_manager/view_tree_state.h"
#include "lib/ftl/macros.h"

namespace view_manager {

// Maintains a registry of the state of all views.
// All ViewState objects are owned by the registry.
class ViewRegistry : public ViewInspector, public InputOwner {
 public:
  explicit ViewRegistry(app::ApplicationContext* application_context,
                        mozart::CompositorPtr compositor);
  ~ViewRegistry() override;

  // VIEW MANAGER REQUESTS

  // Creates a view and returns its ViewToken.
  void CreateView(fidl::InterfaceRequest<mozart::View> view_request,
                  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  mozart::ViewListenerPtr view_listener,
                  const fidl::String& label);

  // Creates a view tree.
  void CreateViewTree(
      fidl::InterfaceRequest<mozart::ViewTree> view_tree_request,
      mozart::ViewTreeListenerPtr view_tree_listener,
      const fidl::String& label);

  // VIEW STUB REQUESTS

  void OnViewResolved(ViewStub* view_stub, mozart::ViewTokenPtr view_token);
  void TransferViewOwner(
      mozart::ViewTokenPtr view_token,
      fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request);

  // VIEW REQUESTS

  // Creates a scene for the view, replacing its current scene.
  // Destroys |view_state| if an error occurs.
  void CreateScene(ViewState* view_state,
                   fidl::InterfaceRequest<mozart::Scene> scene);

  // Invalidates a view.
  // Destroys |view_state| if an error occurs.
  void Invalidate(ViewState* view_state);

  // Called when one of the view pipes is closed remotely.
  void OnViewDied(ViewState* view_state, const std::string& reason);

  // VIEW TREE REQUESTS

  // Sets the view tree's renderer.
  // Destroys |tree_state| if an error occurs.
  void SetRenderer(ViewTreeState* tree_state, mozart::RendererPtr renderer);

  // Called when one of the view tree pipes is closed remotely.
  void OnViewTreeDied(ViewTreeState* tree_state, const std::string& reason);

  // VIEW CONTAINER

  // Adds a child, reparenting it if necessary.
  // Destroys |container_state| if an error occurs.
  void AddChild(ViewContainerState* container_state,
                uint32_t child_key,
                fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner);

  // Removes a child.
  // Destroys |container_state| if an error occurs.
  void RemoveChild(
      ViewContainerState* container_state,
      uint32_t child_key,
      fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request);

  // Sets a child's properties.
  // Destroys |container_state| if an error occurs.
  void SetChildProperties(ViewContainerState* container_state,
                          uint32_t child_key,
                          uint32_t child_scene_version,
                          mozart::ViewPropertiesPtr child_properties);

  // Make child the first responder
  // Destroys |container_state| if an error occurs.
  void RequestFocus(ViewContainerState* container_state, uint32_t child_key);

  // Flushes changes to children.
  // Destroys |container_state| if an error occurs.
  void FlushChildren(ViewContainerState* container_state, uint32_t flush_token);

  // SERVICE PROVIDER REQUESTS

  // Connects to a view service.
  // Destroys |view_state| if an error occurs.
  void ConnectToViewService(ViewState* view_state,
                            const fidl::String& service_name,
                            mx::channel client_handle);

  // Connects to a view service.
  // Destroys |view_state| if an error occurs.
  void ConnectToViewTreeService(ViewTreeState* tree_state,
                                const fidl::String& service_name,
                                mx::channel client_handle);

  // VIEW INSPECTOR REQUESTS

  void GetHitTester(
      mozart::ViewTreeTokenPtr view_tree_token,
      fidl::InterfaceRequest<mozart::HitTester> hit_tester_request,
      const GetHitTesterCallback& callback) override;

  void ResolveScenes(std::vector<mozart::SceneTokenPtr> scene_tokens,
                     const ResolveScenesCallback& callback) override;

  void ResolveFocusChain(mozart::ViewTreeTokenPtr view_tree_token,
                         const ResolveFocusChainCallback& callback) override;
  void ActivateFocusChain(mozart::ViewTokenPtr view_token,
                          const ActivateFocusChainCallback& callback) override;
  void HasFocus(mozart::ViewTokenPtr view_token,
                const HasFocusCallback& callback) override;
  void GetSoftKeyboardContainer(
      mozart::ViewTokenPtr view_token,
      fidl::InterfaceRequest<mozart::SoftKeyboardContainer> container) override;
  void GetImeService(
      mozart::ViewTokenPtr view_token,
      fidl::InterfaceRequest<mozart::ImeService> ime_service) override;
  void ResolveHits(mozart::HitTestResultPtr hit_test_result,
                   const ResolvedHitsCallback& callback) override;

  // Delivers an event to a view.
  void DeliverEvent(const mozart::ViewToken* view_token,
                    mozart::InputEventPtr event,
                    ViewInspector::OnEventDelivered callback) override;

  // Query view for hit test
  void ViewHitTest(
      const mozart::ViewToken* view_token,
      mozart::PointFPtr point,
      const mozart::ViewHitTester::HitTestCallback& callback) override;

  // INPUT CONNECTION CALLBACKS
  void OnInputConnectionDied(InputConnectionImpl* connection) override;

  // INPUT DISPATCHER CALLBACKS
  void OnInputDispatcherDied(InputDispatcherImpl* dispatcher) override;

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
  void TransferOrUnregisterViewStub(
      std::unique_ptr<ViewStub> view_stub,
      fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request);

  // INVALIDATION

  void ScheduleViewInvalidation(ViewState* view_state, uint32_t flags);
  void ScheduleViewTreeInvalidation(ViewTreeState* tree_state, uint32_t flags);
  void TraverseViewTree(ViewTreeState* tree_state,
                        mozart::FrameInfoPtr frame_info);
  void TraverseView(ViewState* view_state,
                    const mozart::FrameInfo* frame_info,
                    bool parent_properties_changed);
  mozart::ViewPropertiesPtr ResolveViewProperties(ViewState* view_state);

  // SCENE MANAGEMENT

  void OnViewSceneTokenAvailable(ftl::WeakPtr<ViewState> view_state_weak,
                                 mozart::SceneTokenPtr scene_token);
  void OnStubSceneTokenAvailable(ftl::WeakPtr<ViewStub> view_stub_weak,
                                 mozart::SceneTokenPtr scene_token);
  void PublishStubScene(ViewState* view_state);

  // RENDERING

  void SetRendererRootScene(ViewTreeState* view_tree);
  void OnRendererDied(ViewTreeState* view_tree);

  // SIGNALING

  void SendInvalidation(ViewState* view_state,
                        mozart::ViewInvalidationPtr invalidation);

  void SendRendererDied(ViewTreeState* tree_state);

  void SendChildAttached(ViewContainerState* container_state,
                         uint32_t child_key,
                         mozart::ViewInfoPtr child_view_info);
  void SendChildUnavailable(ViewContainerState* container_state,
                            uint32_t child_key);

  // INPUT CONNECTION
  void CreateInputConnection(
      mozart::ViewTokenPtr view_token,
      fidl::InterfaceRequest<mozart::InputConnection> request);

  // INPUT DISPATCHER
  void CreateInputDispatcher(
      mozart::ViewTreeTokenPtr view_tree_token,
      fidl::InterfaceRequest<mozart::InputDispatcher> request);

  // ResolveHits Helper
  void ResolveSceneHit(
      const mozart::SceneHit* scene_hit,
      ResolvedHits* resolved_hits,
      std::vector<mozart::SceneTokenPtr>* missing_scene_tokens);
  void OnScenesResolved(std::unique_ptr<ResolvedHits> resolved_hits,
                        std::vector<uint32_t> missing_scene_token_values,
                        const ResolvedHitsCallback& callback,
                        std::vector<mozart::ViewTokenPtr> view_tokens);

  // LOOKUP

  // Walk up the view tree starting at |view_token| to find a service
  // provider that offers a service named |service_name|.
  app::ServiceProvider* FindViewServiceProvider(uint32_t view_token,
                                                std::string service_name);

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

  app::ApplicationContext* application_context_;
  mozart::CompositorPtr compositor_;

  uint32_t next_view_token_value_ = 1u;
  uint32_t next_view_tree_token_value_ = 1u;
  std::unordered_map<uint32_t, ViewState*> views_by_token_;
  std::unordered_map<uint32_t, ViewState*> views_by_scene_token_;
  std::unordered_map<uint32_t, ViewTreeState*> view_trees_by_token_;

  std::unordered_map<uint32_t, std::unique_ptr<InputConnectionImpl>>
      input_connections_by_view_token_;
  std::unordered_map<uint32_t, std::unique_ptr<InputDispatcherImpl>>
      input_dispatchers_by_view_tree_token_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewRegistry);
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_VIEW_REGISTRY_H_
