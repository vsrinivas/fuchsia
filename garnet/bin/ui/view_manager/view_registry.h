// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_REGISTRY_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_REGISTRY_H_

#include <list>
#include <string>
#include <unordered_map>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "garnet/bin/ui/view_manager/view_container_state.h"
#include "garnet/bin/ui/view_manager/view_state.h"
#include "garnet/bin/ui/view_manager/view_stub.h"
#include "garnet/bin/ui/view_manager/view_tree_state.h"
#include "garnet/lib/ui/gfx/engine/object_linker.h"
#include "lib/component/cpp/startup_context.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "lib/ui/scenic/cpp/session.h"

namespace view_manager {

using ViewLinker = scenic_impl::gfx::ObjectLinker<ViewStub, ViewState>;

// Maintains a registry of the state of all views.
// All ViewState objects are owned by the registry.
class ViewRegistry : public scenic_impl::ErrorReporter {
 public:
  explicit ViewRegistry(component::StartupContext* startup_context);
  virtual ~ViewRegistry();

  // |ErrorReporter|
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override {
    FXL_LOG(ERROR) << error_string;
  }

  // VIEW MANAGER REQUESTS

  void GetScenic(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> scenic_request);
  void CreateView(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
      zx::eventpair view_token,
      ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener,
      zx::eventpair parent_export_token, fidl::StringPtr label);
  void CreateViewTree(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewTree>
          view_tree_request,
      ::fuchsia::ui::viewsv1::ViewTreeListenerPtr view_tree_listener,
      fidl::StringPtr label);

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
                zx::eventpair view_holder_token,
                zx::eventpair host_import_token);

  // Removes a child.
  // Destroys |container_state| if an error occurs.
  void RemoveChild(ViewContainerState* container_state, uint32_t child_key,
                   zx::eventpair transferred_view_holder_token);

  // Sets a child's properties.
  // Destroys |container_state| if an error occurs.
  void SetChildProperties(
      ViewContainerState* container_state, uint32_t child_key,
      ::fuchsia::ui::viewsv1::ViewPropertiesPtr child_properties);

  // Sets a hint on changes to a child's target size.
  // Destroys |container_state| if an error occurs.
  void SendSizeChangeHintHACK(ViewContainerState* container_state,
                              uint32_t child_key, float width_change_factor,
                              float size_change_factor);

  void RequestSnapshotHACK(
      ViewContainerState* container_state, uint32_t child_key,
      fit::function<void(::fuchsia::mem::Buffer)> callback);

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

  // SNAPSHOT
  void TakeSnapshot(uint64_t view_koid,
                    fit::function<void(::fuchsia::mem::Buffer)> callback);

  // SIGNALING

  void SendChildAttached(ViewContainerState* container_state,
                         uint32_t child_key,
                         ::fuchsia::ui::viewsv1::ViewInfo child_view_info);
  void SendChildUnavailable(ViewContainerState* container_state,
                            uint32_t child_key);

  // Transferring views.
  std::unique_ptr<ViewContainerState::ChildView> FindOrphanedView(
      zx_handle_t view_holder_token);
  void AddOrphanedView(zx::eventpair transferred_view_token,
                       std::unique_ptr<ViewContainerState::ChildView> child);
  void RemoveOrphanedView(ViewContainerState::ChildView* child);

 private:
  // LIFETIME

  void UnregisterView(ViewState* view_state);
  void UnregisterViewTree(ViewTreeState* tree_state);
  void UnregisterViewContainer(ViewContainerState* container_state);
  void UnregisterViewStub(std::unique_ptr<ViewStub> view_stub);
  void UnregisterChildren(ViewContainerState* container_state);
  void ReleaseViewStubChildHost(ViewStub* view_stub);

  // SESSION MANAGEMENT

  void SchedulePresentSession();
  void PresentSession();

  // LOOKUP

  // Walk up the view tree starting at |view_token| to find a service
  // provider that offers a service named |service_name|.
  fuchsia::sys::ServiceProvider* FindViewServiceProvider(
      ViewState* view, std::string service_name);

  ViewState* FindView(uint32_t view_token);
  ViewTreeState* FindViewTree(uint32_t view_tree_token_value);

  bool IsViewStateRegisteredDebug(ViewState* view_state) {
    return view_state && FindView(view_state->view_token());
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

  component::StartupContext* startup_context_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  scenic::Session session_;

  bool present_session_scheduled_ = false;
  uint32_t next_view_id_value_ = 1u;
  uint32_t next_view_tree_token_value_ = 1u;

  struct OrphanedView {
    zx::eventpair view_holder_token;
    std::unique_ptr<ViewContainerState::ChildView> child_view;
  };
  std::map<zx_koid_t, OrphanedView> orphaned_views_;
  std::unordered_map<uint32_t, std::unique_ptr<ViewState>> views_by_token_;
  std::unordered_map<uint32_t, std::unique_ptr<ViewTreeState>>
      view_trees_by_token_;
  std::list<std::shared_ptr<::fuchsia::ui::gfx::SnapshotCallbackHACK>>
      snapshot_bindings_;

  fxl::WeakPtrFactory<ViewRegistry> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewRegistry);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_REGISTRY_H_
