// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_REGISTRY_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_REGISTRY_H_

#include <string>
#include <unordered_map>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "garnet/bin/ui/view_manager/input/input_connection_impl.h"
#include "garnet/bin/ui/view_manager/input/input_dispatcher_impl.h"
#include "garnet/bin/ui/view_manager/internal/input_owner.h"
#include "garnet/bin/ui/view_manager/internal/view_inspector.h"
#include "garnet/bin/ui/view_manager/view_container_state.h"
#include "garnet/bin/ui/view_manager/view_state.h"
#include "garnet/bin/ui/view_manager/view_stub.h"
#include "garnet/bin/ui/view_manager/view_tree_state.h"

#include "lib/component/cpp/startup_context.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/ui/scenic/cpp/session.h"

namespace view_manager {

// Maintains a registry of the state of all views.
// All ViewState objects are owned by the registry.
class ViewRegistry : public ViewInspector,
                     public InputOwner,
                     public fuchsia::ui::viewsv1::AccessibilityViewInspector {
 public:
  explicit ViewRegistry(component::StartupContext* startup_context);
  ~ViewRegistry() override;

  // VIEW MANAGER REQUESTS

  void GetScenic(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> scenic_request);
  void CreateView(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request,
      ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener,
      zx::eventpair parent_export_token, fidl::StringPtr label);
  void CreateViewTree(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewTree>
          view_tree_request,
      ::fuchsia::ui::viewsv1::ViewTreeListenerPtr view_tree_listener,
      fidl::StringPtr label);

  // VIEW STUB REQUESTS

  void OnViewResolved(ViewStub* view_stub,
                      ::fuchsia::ui::viewsv1token::ViewToken view_token,
                      bool success);
  void TransferViewOwner(
      ::fuchsia::ui::viewsv1token::ViewToken view_token,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          transferred_view_owner_request);

  // VIEW REQUESTS

  // Called when one of the view pipes is closed remotely.
  void OnViewDied(ViewState* view_state, const std::string& reason);

  // VIEW TREE REQUESTS

  // Called when one of the view tree pipes is closed remotely.
  void OnViewTreeDied(ViewTreeState* tree_state, const std::string& reason);

  // VIEW CONTAINER

  // Adds a child, reparenting it if necessary.
  // Destroys |container_state| if an error occurs.
  void AddChild(ViewContainerState* container_state, uint32_t child_key,
                fidl::InterfaceHandle<::fuchsia::ui::viewsv1token::ViewOwner>
                    child_view_owner,
                zx::eventpair host_import_token);

  // Removes a child.
  // Destroys |container_state| if an error occurs.
  void RemoveChild(
      ViewContainerState* container_state, uint32_t child_key,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          transferred_view_owner_request);

  // Sets a child's properties.
  // Destroys |container_state| if an error occurs.
  void SetChildProperties(
      ViewContainerState* container_state, uint32_t child_key,
      ::fuchsia::ui::viewsv1::ViewPropertiesPtr child_properties);

  // Make child the first responder
  // Destroys |container_state| if an error occurs.
  void RequestFocus(ViewContainerState* container_state, uint32_t child_key);

  // SERVICE PROVIDER REQUESTS

  // Connects to a view service.
  // Destroys |view_state| if an error occurs.
  void ConnectToViewService(ViewState* view_state,
                            const fidl::StringPtr& service_name,
                            zx::channel client_handle);

  // Connects to a view service.
  // Destroys |view_state| if an error occurs.
  void ConnectToViewTreeService(ViewTreeState* tree_state,
                                const fidl::StringPtr& service_name,
                                zx::channel client_handle);

  // VIEW INSPECTOR REQUESTS

  void HitTest(::fuchsia::ui::viewsv1::ViewTreeToken view_tree_token,
               const fuchsia::math::Point3F& ray_origin,
               const fuchsia::math::Point3F& ray_direction,
               HitTestCallback callback) override;
  void ResolveFocusChain(::fuchsia::ui::viewsv1::ViewTreeToken view_tree_token,
                         ResolveFocusChainCallback callback) override;
  void ActivateFocusChain(::fuchsia::ui::viewsv1token::ViewToken view_token,
                          ActivateFocusChainCallback callback) override;
  void HasFocus(::fuchsia::ui::viewsv1token::ViewToken view_token,
                HasFocusCallback callback) override;
  void GetSoftKeyboardContainer(
      ::fuchsia::ui::viewsv1token::ViewToken view_token,
      fidl::InterfaceRequest<fuchsia::ui::input::SoftKeyboardContainer>
          container) override;
  void GetImeService(::fuchsia::ui::viewsv1token::ViewToken view_token,
                     fidl::InterfaceRequest<fuchsia::ui::input::ImeService>
                         ime_service) override;

  // Delivers an event to a view.
  void DeliverEvent(::fuchsia::ui::viewsv1token::ViewToken view_token,
                    fuchsia::ui::input::InputEvent event,
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
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          transferred_view_owner_request);

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
  ::fuchsia::ui::viewsv1::ViewPropertiesPtr ResolveViewProperties(
      ViewState* view_state);

  // SESSION MANAGEMENT

  void SchedulePresentSession();
  void PresentSession();

  // SIGNALING

  void SendPropertiesChanged(ViewState* view_state,
                             ::fuchsia::ui::viewsv1::ViewProperties properties);
  void SendChildAttached(ViewContainerState* container_state,
                         uint32_t child_key,
                         ::fuchsia::ui::viewsv1::ViewInfo child_view_info);
  void SendChildUnavailable(ViewContainerState* container_state,
                            uint32_t child_key);

  // INPUT CONNECTION
  void CreateInputConnection(
      ::fuchsia::ui::viewsv1token::ViewToken view_token,
      fidl::InterfaceRequest<fuchsia::ui::input::InputConnection> request);

  // INPUT DISPATCHER
  void CreateInputDispatcher(
      ::fuchsia::ui::viewsv1::ViewTreeToken view_tree_token,
      fidl::InterfaceRequest<fuchsia::ui::input::InputDispatcher> request);

  // LOOKUP

  // Walk up the view tree starting at |view_token| to find a service
  // provider that offers a service named |service_name|.
  fuchsia::sys::ServiceProvider* FindViewServiceProvider(
      uint32_t view_token, std::string service_name);

  ViewState* FindView(uint32_t view_token_value);
  ViewTreeState* FindViewTree(uint32_t view_tree_token_value);

  bool IsViewStateRegisteredDebug(ViewState* view_state) {
    return view_state && FindView(view_state->view_token().value);
  }

  bool IsViewTreeStateRegisteredDebug(ViewTreeState* tree_state) {
    return tree_state && FindViewTree(tree_state->view_tree_token().value);
  }

  bool IsViewContainerStateRegisteredDebug(
      ViewContainerState* container_state) {
    return container_state &&
           (IsViewStateRegisteredDebug(container_state->AsViewState()) ||
            IsViewTreeStateRegisteredDebug(container_state->AsViewTreeState()));
  }

  // A11Y CLIENTS

  // Calls a view's accessibility service if it exists.
  void A11yNotifyViewSelected(
      ::fuchsia::ui::viewsv1token::ViewToken view_token);

  // A11Y VIEW INSPECTOR

  // Performs a view hit-test on the view tree corresponding to
  // the associated token and returns a vector of gfx::Hit objects
  // corresponding to the views hit, in order of first to last hit.
  void PerformHitTest(fuchsia::ui::viewsv1::ViewTreeToken token,
                      fuchsia::math::Point3F origin,
                      fuchsia::math::Point3F direction,
                      PerformHitTestCallback callback) override;

  component::StartupContext* startup_context_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  scenic::Session session_;

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
