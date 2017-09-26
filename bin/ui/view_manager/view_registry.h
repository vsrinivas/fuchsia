// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_REGISTRY_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_REGISTRY_H_

#include <string>
#include <unordered_map>

#include "lib/app/cpp/application_context.h"
#include "lib/ui/scenic/client/session.h"
#include "lib/ui/scenic/fidl/scene_manager.fidl.h"
#include "lib/ui/views/fidl/view_trees.fidl.h"
#include "lib/ui/views/fidl/views.fidl.h"
#include "garnet/bin/ui/view_manager/input/input_connection_impl.h"
#include "garnet/bin/ui/view_manager/input/input_dispatcher_impl.h"
#include "garnet/bin/ui/view_manager/internal/input_owner.h"
#include "garnet/bin/ui/view_manager/internal/view_inspector.h"
#include "garnet/bin/ui/view_manager/view_container_state.h"
#include "garnet/bin/ui/view_manager/view_state.h"
#include "garnet/bin/ui/view_manager/view_stub.h"
#include "garnet/bin/ui/view_manager/view_tree_state.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace view_manager {

// Maintains a registry of the state of all views.
// All ViewState objects are owned by the registry.
class ViewRegistry : public ViewInspector, public InputOwner {
 public:
  explicit ViewRegistry(app::ApplicationContext* application_context);
  ~ViewRegistry() override;

  // VIEW MANAGER REQUESTS

  void GetSceneManager(
      fidl::InterfaceRequest<scenic::SceneManager> scene_manager_request);
  void CreateView(fidl::InterfaceRequest<mozart::View> view_request,
                  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  mozart::ViewListenerPtr view_listener,
                  zx::eventpair parent_export_token,
                  const fidl::String& label);
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

  // Called when one of the view pipes is closed remotely.
  void OnViewDied(ViewState* view_state, const std::string& reason);

  // VIEW TREE REQUESTS

  // Called when one of the view tree pipes is closed remotely.
  void OnViewTreeDied(ViewTreeState* tree_state, const std::string& reason);

  // VIEW CONTAINER

  // Adds a child, reparenting it if necessary.
  // Destroys |container_state| if an error occurs.
  void AddChild(ViewContainerState* container_state,
                uint32_t child_key,
                fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner,
                zx::eventpair host_import_token);

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
                          mozart::ViewPropertiesPtr child_properties);

  // Make child the first responder
  // Destroys |container_state| if an error occurs.
  void RequestFocus(ViewContainerState* container_state, uint32_t child_key);

  // SERVICE PROVIDER REQUESTS

  // Connects to a view service.
  // Destroys |view_state| if an error occurs.
  void ConnectToViewService(ViewState* view_state,
                            const fidl::String& service_name,
                            zx::channel client_handle);

  // Connects to a view service.
  // Destroys |view_state| if an error occurs.
  void ConnectToViewTreeService(ViewTreeState* tree_state,
                                const fidl::String& service_name,
                                zx::channel client_handle);

  // VIEW INSPECTOR REQUESTS

  void HitTest(const mozart::ViewTreeToken& view_tree_token,
               const mozart::PointF& point,
               HitTestCallback callback) override;
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

  // Delivers an event to a view.
  void DeliverEvent(const mozart::ViewToken* view_token,
                    mozart::InputEventPtr event,
                    ViewInspector::OnEventDelivered callback) override;

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
  void ReleaseViewStubChildHost(ViewStub* view_stub);

  // TREE MANIPULATION

  void AttachResolvedViewAndNotify(ViewStub* view_stub, ViewState* view_state);
  void ReleaseUnavailableViewAndNotify(ViewStub* view_stub);
  void HijackView(ViewState* view_state);
  void TransferOrUnregisterViewStub(
      std::unique_ptr<ViewStub> view_stub,
      fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request);

  // INVALIDATION

  // Makes note of the fact that a view or view tree has changed in some
  // manner to be applied during upcoming traversals.
  void InvalidateView(ViewState* view_state, uint32_t flags);
  void InvalidateViewTree(ViewTreeState* tree_state, uint32_t flags);

  // TRAVERSAL

  void ScheduleTraversal();

  // Traverses views delivering updates to view properties in response to prior
  // invalidations.
  void Traverse();
  void TraverseViewTree(ViewTreeState* tree_state);
  void TraverseView(ViewState* view_state, bool parent_properties_changed);
  mozart::ViewPropertiesPtr ResolveViewProperties(ViewState* view_state);

  // SESSION MANAGEMENT

  void SchedulePresentSession();
  void PresentSession();

  // SIGNALING

  void SendPropertiesChanged(ViewState* view_state,
                             mozart::ViewPropertiesPtr properties);
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
  scenic::SceneManagerPtr scene_manager_;
  scenic_lib::Session session_;

  bool traversal_scheduled_ = false;
  bool present_session_scheduled_ = false;

  uint32_t next_view_token_value_ = 1u;
  uint32_t next_view_tree_token_value_ = 1u;
  std::unordered_map<uint32_t, ViewState*> views_by_token_;
  std::unordered_map<uint32_t, ViewTreeState*> view_trees_by_token_;

  std::unordered_map<uint32_t, std::unique_ptr<InputConnectionImpl>>
      input_connections_by_view_token_;
  std::unordered_map<uint32_t, std::unique_ptr<InputDispatcherImpl>>
      input_dispatchers_by_view_tree_token_;

  fxl::WeakPtrFactory<ViewRegistry> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewRegistry);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_REGISTRY_H_
