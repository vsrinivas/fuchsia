// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_VIEW_FRAMEWORK_BASE_VIEW_H_
#define APPS_MOZART_LIB_VIEW_FRAMEWORK_BASE_VIEW_H_

#include <memory>
#include <string>

#include "application/services/service_provider.fidl.h"
#include "apps/mozart/lib/scene/client/resources.h"
#include "apps/mozart/lib/scene/client/session.h"
#include "apps/mozart/services/input/input_connection.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace mozart {

// Abstract base implementation of a view for simple applications.
// Subclasses must handle layout and provide content for the scene by
// implementing the methods of the |ViewListener| fidl interface.
//
// It is not necessary to use this class to implement all Views.
// This class is merely intended to make the simple apps easier to write.
class BaseView : private ViewListener,
                 private ViewContainerListener,
                 private mozart::InputListener {
 public:
  BaseView(ViewManagerPtr view_manager,
           fidl::InterfaceRequest<ViewOwner> view_owner_request,
           const std::string& label);

  ~BaseView() override;

  // Gets the view manager.
  ViewManager* view_manager() { return view_manager_.get(); }

  // Gets the underlying view interface.
  View* view() { return view_.get(); }

  // Gets the service provider for the view.
  app::ServiceProvider* GetViewServiceProvider();

  // Gets the underlying view container interface.
  ViewContainer* GetViewContainer();

  // Gets a wrapper for the view's session.
  mozart::client::Session* session() { return &session_; }

  // Gets the imported parent node to which the session's tree of nodes
  // should be attached.
  mozart::client::ImportNode& parent_node() { return parent_node_; }

  // Gets the current view properties.
  // Returns nullptr if unknown.
  const ViewProperties* properties() const { return properties_.get(); }

  // Returns true if the view has a non-empty size.
  bool has_size() const { return size_.width > 0 && size_.height > 0; }

  // Gets the size of the view.
  const mozart::Size& size() const { return size_; }

  // Gets the view's device pixel ratio.
  float device_pixel_ratio() const {
    return properties_ ? properties_->display_metrics->device_pixel_ratio : 1.f;
  }

  // Gets the input connection.
  InputConnection* input_connection() { return input_connection_.get(); }

  // Sets a callback which is invoked when the view's owner releases the
  // view causing the view manager to unregister it.
  //
  // This should be used to implement cleanup policies to release resources
  // associated with the view (including the object itself).
  void SetReleaseHandler(ftl::Closure callback);

  // Invalidates the scene, causing |OnSceneInvalidated()| to be invoked
  // during the next frame.
  void InvalidateScene();

  // Called when the view's properties have changed.
  //
  // The subclass should compare the old and new properties and make note of
  // whether these property changes will affect the layout or content of
  // the view then update accordingly.
  //
  // The default implementation does nothing.
  virtual void OnPropertiesChanged(ViewPropertiesPtr old_properties);

  // Called when it's time for the view to update its scene contents due to
  // invalidation.  The new contents are presented once this function returns.
  //
  // The default implementation does nothing.
  virtual void OnSceneInvalidated(
      mozart2::PresentationInfoPtr presentation_info);

  // Called to handle an input event.
  // Returns true if the view will handle the event, false if the event
  // should continue propagating to other views which may handle it themselves.
  //
  // The default implementation returns false.
  virtual bool OnInputEvent(mozart::InputEventPtr event);

  // Called when a child is attached.
  virtual void OnChildAttached(uint32_t child_key, ViewInfoPtr child_view_info);

  // Called when a child becomes unavailable.
  virtual void OnChildUnavailable(uint32_t child_key);

 private:
  // |ViewListener|:
  void OnPropertiesChanged(
      ViewPropertiesPtr properties,
      const OnPropertiesChangedCallback& callback) override;

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key,
                       ViewInfoPtr child_view_info,
                       const OnChildAttachedCallback& callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          const OnChildUnavailableCallback& callback) override;

  // |InputListener|:
  void OnEvent(mozart::InputEventPtr event,
               const OnEventCallback& callback) override;

  void PresentScene();

  ViewManagerPtr view_manager_;
  fidl::Binding<ViewListener> view_listener_binding_;
  fidl::Binding<ViewContainerListener> view_container_listener_binding_;
  fidl::Binding<InputListener> input_listener_binding_;

  ViewPtr view_;
  app::ServiceProviderPtr view_service_provider_;
  ViewContainerPtr view_container_;
  InputConnectionPtr input_connection_;
  ViewPropertiesPtr properties_;
  Size size_;
  mozart::client::Session session_;
  mozart::client::ImportNode parent_node_;

  bool invalidate_pending_ = false;
  bool present_pending_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(BaseView);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_VIEW_FRAMEWORK_BASE_VIEW_H_
